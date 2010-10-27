/*
 * Copyright 2010 Tilera Corporation. All Rights Reserved.
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 *   NON INFRINGEMENT.  See the GNU General Public License for
 *   more details.
 */

#include <linux/string.h>
#include <linux/smp.h>
#include <linux/module.h>
#include <linux/uaccess.h>
#include <asm/fixmap.h>
#include <asm/kmap_types.h>
#include <asm/tlbflush.h>
#include <hv/hypervisor.h>
#include <arch/chip.h>


#if !CHIP_HAS_COHERENT_LOCAL_CACHE()

/* Defined in memcpy.S */
extern unsigned long __memcpy_asm(void *to, const void *from, unsigned long n);
extern unsigned long __copy_to_user_inatomic_asm(
	void __user *to, const void *from, unsigned long n);
extern unsigned long __copy_from_user_inatomic_asm(
	void *to, const void __user *from, unsigned long n);
extern unsigned long __copy_from_user_zeroing_asm(
	void *to, const void __user *from, unsigned long n);

typedef unsigned long (*memcpy_t)(void *, const void *, unsigned long);

/* Size above which to consider TLB games for performance */
#define LARGE_COPY_CUTOFF 2048

/* Communicate to the simulator what we are trying to do. */
#define sim_allow_multiple_caching(b) \
  __insn_mtspr(SPR_SIM_CONTROL, \
   SIM_CONTROL_ALLOW_MULTIPLE_CACHING | ((b) << _SIM_CONTROL_OPERATOR_BITS))

/*
 * Copy memory by briefly enabling incoherent cacheline-at-a-time mode.
 *
 * We set up our own source and destination PTEs that we fully control.
 * This is the only way to guarantee that we don't race with another
 * thread that is modifying the PTE; we can't afford to try the
 * copy_{to,from}_user() technique of catching the interrupt, since
 * we must run with interrupts disabled to avoid the risk of some
 * other code seeing the incoherent data in our cache.  (Recall that
 * our cache is indexed by PA, so even if the other code doesn't use
 * our KM_MEMCPY virtual addresses, they'll still hit in cache using
 * the normal VAs that aren't supposed to hit in cache.)
 */
static void memcpy_multicache(void *dest, const void *source,
			      pte_t dst_pte, pte_t src_pte, int len)
{
	int idx;
	unsigned long flags, newsrc, newdst;
	pmd_t *pmdp;
	pte_t *ptep;
	int cpu = get_cpu();

	/*
	 * Disable interrupts so that we don't recurse into memcpy()
	 * in an interrupt handler, nor accidentally reference
	 * the PA of the source from an interrupt routine.  Also
	 * notify the simulator that we're playing games so we don't
	 * generate spurious coherency warnings.
	 */
	local_irq_save(flags);
	sim_allow_multiple_caching(1);

	/* Set up the new dest mapping */
	idx = FIX_KMAP_BEGIN + (KM_TYPE_NR * cpu) + KM_MEMCPY0;
	newdst = __fix_to_virt(idx) + ((unsigned long)dest & (PAGE_SIZE-1));
	pmdp = pmd_offset(pud_offset(pgd_offset_k(newdst), newdst), newdst);
	ptep = pte_offset_kernel(pmdp, newdst);
	if (pte_val(*ptep) != pte_val(dst_pte)) {
		set_pte(ptep, dst_pte);
		local_flush_tlb_page(NULL, newdst, PAGE_SIZE);
	}

	/* Set up the new source mapping */
	idx += (KM_MEMCPY0 - KM_MEMCPY1);
	src_pte = hv_pte_set_nc(src_pte);
	src_pte = hv_pte_clear_writable(src_pte);  /* be paranoid */
	newsrc = __fix_to_virt(idx) + ((unsigned long)source & (PAGE_SIZE-1));
	pmdp = pmd_offset(pud_offset(pgd_offset_k(newsrc), newsrc), newsrc);
	ptep = pte_offset_kernel(pmdp, newsrc);
	*ptep = src_pte;   /* set_pte() would be confused by this */
	local_flush_tlb_page(NULL, newsrc, PAGE_SIZE);

	/* Actually move the data. */
	__memcpy_asm((void *)newdst, (const void *)newsrc, len);

	/*
	 * Remap the source as locally-cached and not OLOC'ed so that
	 * we can inval without also invaling the remote cpu's cache.
	 * This also avoids known errata with inv'ing cacheable oloc data.
	 */
	src_pte = hv_pte_set_mode(src_pte, HV_PTE_MODE_CACHE_NO_L3);
	src_pte = hv_pte_set_writable(src_pte); /* need write access for inv */
	*ptep = src_pte;   /* set_pte() would be confused by this */
	local_flush_tlb_page(NULL, newsrc, PAGE_SIZE);

	/*
	 * Do the actual invalidation, covering the full L2 cache line
	 * at the end since __memcpy_asm() is somewhat aggressive.
	 */
	__inv_buffer((void *)newsrc, len);

	/*
	 * We're done: notify the simulator that all is back to normal,
	 * and re-enable interrupts and pre-emption.
	 */
	sim_allow_multiple_caching(0);
	local_irq_restore(flags);
	put_cpu();
}

/*
 * Identify large copies from remotely-cached memory, and copy them
 * via memcpy_multicache() if they look good, otherwise fall back
 * to the particular kind of copying passed as the memcpy_t function.
 */
static unsigned long fast_copy(void *dest, const void *source, int len,
			       memcpy_t func)
{
	/*
	 * Check if it's big enough to bother with.  We may end up doing a
	 * small copy via TLB manipulation if we're near a page boundary,
	 * but presumably we'll make it up when we hit the second page.
	 */
	while (len >= LARGE_COPY_CUTOFF) {
		int copy_size, bytes_left_on_page;
		pte_t *src_ptep, *dst_ptep;
		pte_t src_pte, dst_pte;
		struct page *src_page, *dst_page;

		/* Is the source page oloc'ed to a remote cpu? */
retry_source:
		src_ptep = virt_to_pte(current->mm, (unsigned long)source);
		if (src_ptep == NULL)
			break;
		src_pte = *src_ptep;
		if (!hv_pte_get_present(src_pte) ||
		    !hv_pte_get_readable(src_pte) ||
		    hv_pte_get_mode(src_pte) != HV_PTE_MODE_CACHE_TILE_L3)
			break;
		if (get_remote_cache_cpu(src_pte) == smp_processor_id())
			break;
		src_page = pfn_to_page(hv_pte_get_pfn(src_pte));
		get_page(src_page);
		if (pte_val(src_pte) != pte_val(*src_ptep)) {
			put_page(src_page);
			goto retry_source;
		}
		if (pte_huge(src_pte)) {
			/* Adjust the PTE to correspond to a small page */
			int pfn = hv_pte_get_pfn(src_pte);
			pfn += (((unsigned long)source & (HPAGE_SIZE-1))
				>> PAGE_SHIFT);
			src_pte = pfn_pte(pfn, src_pte);
			src_pte = pte_mksmall(src_pte);
		}

		/* Is the destination page writable? */
retry_dest:
		dst_ptep = virt_to_pte(current->mm, (unsigned long)dest);
		if (dst_ptep == NULL) {
			put_page(src_page);
			break;
		}
		dst_pte = *dst_ptep;
		if (!hv_pte_get_present(dst_pte) ||
		    !hv_pte_get_writable(dst_pte)) {
			put_page(src_page);
			break;
		}
		dst_page = pfn_to_page(hv_pte_get_pfn(dst_pte));
		if (dst_page == src_page) {
			/*
			 * Source and dest are on the same page; this
			 * potentially exposes us to incoherence if any
			 * part of src and dest overlap on a cache line.
			 * Just give up rather than trying to be precise.
			 */
			put_page(src_page);
			break;
		}
		get_page(dst_page);
		if (pte_val(dst_pte) != pte_val(*dst_ptep)) {
			put_page(dst_page);
			goto retry_dest;
		}
		if (pte_huge(dst_pte)) {
			/* Adjust the PTE to correspond to a small page */
			int pfn = hv_pte_get_pfn(dst_pte);
			pfn += (((unsigned long)dest & (HPAGE_SIZE-1))
				>> PAGE_SHIFT);
			dst_pte = pfn_pte(pfn, dst_pte);
			dst_pte = pte_mksmall(dst_pte);
		}

		/* All looks good: create a cachable PTE and copy from it */
		copy_size = len;
		bytes_left_on_page =
			PAGE_SIZE - (((int)source) & (PAGE_SIZE-1));
		if (copy_size > bytes_left_on_page)
			copy_size = bytes_left_on_page;
		bytes_left_on_page =
			PAGE_SIZE - (((int)dest) & (PAGE_SIZE-1));
		if (copy_size > bytes_left_on_page)
			copy_size = bytes_left_on_page;
		memcpy_multicache(dest, source, dst_pte, src_pte, copy_size);

		/* Release the pages */
		put_page(dst_page);
		put_page(src_page);

		/* Continue on the next page */
		dest += copy_size;
		source += copy_size;
		len -= copy_size;
	}

	return func(dest, source, len);
}

void *memcpy(void *to, const void *from, __kernel_size_t n)
{
	if (n < LARGE_COPY_CUTOFF)
		return (void *)__memcpy_asm(to, from, n);
	else
		return (void *)fast_copy(to, from, n, __memcpy_asm);
}

unsigned long __copy_to_user_inatomic(void __user *to, const void *from,
				      unsigned long n)
{
	if (n < LARGE_COPY_CUTOFF)
		return __copy_to_user_inatomic_asm(to, from, n);
	else
		return fast_copy(to, from, n, __copy_to_user_inatomic_asm);
}

unsigned long __copy_from_user_inatomic(void *to, const void __user *from,
					unsigned long n)
{
	if (n < LARGE_COPY_CUTOFF)
		return __copy_from_user_inatomic_asm(to, from, n);
	else
		return fast_copy(to, from, n, __copy_from_user_inatomic_asm);
}

unsigned long __copy_from_user_zeroing(void *to, const void __user *from,
				       unsigned long n)
{
	if (n < LARGE_COPY_CUTOFF)
		return __copy_from_user_zeroing_asm(to, from, n);
	else
		return fast_copy(to, from, n, __copy_from_user_zeroing_asm);
}

#endif /* !CHIP_HAS_COHERENT_LOCAL_CACHE() */
