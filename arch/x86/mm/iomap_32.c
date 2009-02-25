/*
 * Copyright © 2008 Ingo Molnar
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 */

#include <asm/iomap.h>
#include <asm/pat.h>
#include <linux/module.h>

#ifdef CONFIG_X86_PAE
static int
is_io_mapping_possible(resource_size_t base, unsigned long size)
{
	return 1;
}
#else
static int
is_io_mapping_possible(resource_size_t base, unsigned long size)
{
	/* There is no way to map greater than 1 << 32 address without PAE */
	if (base + size > 0x100000000ULL)
		return 0;

	return 1;
}
#endif

int
reserve_io_memtype_wc(u64 base, unsigned long size, pgprot_t *prot)
{
	unsigned long ret_flag;

	if (!is_io_mapping_possible(base, size))
		goto out_err;

	if (!pat_enabled) {
		*prot = pgprot_noncached(PAGE_KERNEL);
		return 0;
	}

	if (reserve_memtype(base, base + size, _PAGE_CACHE_WC, &ret_flag))
		goto out_err;

	if (ret_flag == _PAGE_CACHE_WB)
		goto out_free;

	if (kernel_map_sync_memtype(base, size, ret_flag))
		goto out_free;

	*prot = __pgprot(__PAGE_KERNEL | ret_flag);
	return 0;

out_free:
	free_memtype(base, base + size);
out_err:
	return -EINVAL;
}

void
free_io_memtype(u64 base, unsigned long size)
{
	if (pat_enabled)
		free_memtype(base, base + size);
}

/* Map 'pfn' using fixed map 'type' and protections 'prot'
 */
void *
iomap_atomic_prot_pfn(unsigned long pfn, enum km_type type, pgprot_t prot)
{
	enum fixed_addresses idx;
	unsigned long vaddr;

	pagefault_disable();

	/*
	 * For non-PAT systems, promote PAGE_KERNEL_WC to PAGE_KERNEL_UC_MINUS.
	 * PAGE_KERNEL_WC maps to PWT, which translates to uncached if the
	 * MTRR is UC or WC.  UC_MINUS gets the real intention, of the
	 * user, which is "WC if the MTRR is WC, UC if you can't do that."
	 */
	if (!pat_enabled && pgprot_val(prot) == pgprot_val(PAGE_KERNEL_WC))
		prot = PAGE_KERNEL_UC_MINUS;

	idx = type + KM_TYPE_NR*smp_processor_id();
	vaddr = __fix_to_virt(FIX_KMAP_BEGIN + idx);
	set_pte(kmap_pte-idx, pfn_pte(pfn, prot));
	arch_flush_lazy_mmu_mode();

	return (void*) vaddr;
}
EXPORT_SYMBOL_GPL(iomap_atomic_prot_pfn);

void
iounmap_atomic(void *kvaddr, enum km_type type)
{
	unsigned long vaddr = (unsigned long) kvaddr & PAGE_MASK;
	enum fixed_addresses idx = type + KM_TYPE_NR*smp_processor_id();

	/*
	 * Force other mappings to Oops if they'll try to access this pte
	 * without first remap it.  Keeping stale mappings around is a bad idea
	 * also, in case the page changes cacheability attributes or becomes
	 * a protected page in a hypervisor.
	 */
	if (vaddr == __fix_to_virt(FIX_KMAP_BEGIN+idx))
		kpte_clear_flush(kmap_pte-idx, vaddr);

	arch_flush_lazy_mmu_mode();
	pagefault_enable();
}
EXPORT_SYMBOL_GPL(iounmap_atomic);
