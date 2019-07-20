/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_SET_MEMORY_H
#define _ASM_X86_SET_MEMORY_H

#include <asm/page.h>
#include <asm-generic/set_memory.h>

/*
 * The set_memory_* API can be used to change various attributes of a virtual
 * address range. The attributes include:
 * Cachability   : UnCached, WriteCombining, WriteThrough, WriteBack
 * Executability : eXeutable, NoteXecutable
 * Read/Write    : ReadOnly, ReadWrite
 * Presence      : NotPresent
 * Encryption    : Encrypted, Decrypted
 *
 * Within a category, the attributes are mutually exclusive.
 *
 * The implementation of this API will take care of various aspects that
 * are associated with changing such attributes, such as:
 * - Flushing TLBs
 * - Flushing CPU caches
 * - Making sure aliases of the memory behind the mapping don't violate
 *   coherency rules as defined by the CPU in the system.
 *
 * What this API does not do:
 * - Provide exclusion between various callers - including callers that
 *   operation on other mappings of the same physical page
 * - Restore default attributes when a page is freed
 * - Guarantee that mappings other than the requested one are
 *   in any state, other than that these do not violate rules for
 *   the CPU you have. Do not depend on any effects on other mappings,
 *   CPUs other than the one you have may have more relaxed rules.
 * The caller is required to take care of these.
 */

int _set_memory_uc(unsigned long addr, int numpages);
int _set_memory_wc(unsigned long addr, int numpages);
int _set_memory_wt(unsigned long addr, int numpages);
int _set_memory_wb(unsigned long addr, int numpages);
int set_memory_uc(unsigned long addr, int numpages);
int set_memory_wc(unsigned long addr, int numpages);
int set_memory_wt(unsigned long addr, int numpages);
int set_memory_wb(unsigned long addr, int numpages);
int set_memory_np(unsigned long addr, int numpages);
int set_memory_4k(unsigned long addr, int numpages);
int set_memory_encrypted(unsigned long addr, int numpages);
int set_memory_decrypted(unsigned long addr, int numpages);
int set_memory_np_noalias(unsigned long addr, int numpages);

int set_memory_array_uc(unsigned long *addr, int addrinarray);
int set_memory_array_wc(unsigned long *addr, int addrinarray);
int set_memory_array_wt(unsigned long *addr, int addrinarray);
int set_memory_array_wb(unsigned long *addr, int addrinarray);

int set_pages_array_uc(struct page **pages, int addrinarray);
int set_pages_array_wc(struct page **pages, int addrinarray);
int set_pages_array_wt(struct page **pages, int addrinarray);
int set_pages_array_wb(struct page **pages, int addrinarray);

/*
 * For legacy compatibility with the old APIs, a few functions
 * are provided that work on a "struct page".
 * These functions operate ONLY on the 1:1 kernel mapping of the
 * memory that the struct page represents, and internally just
 * call the set_memory_* function. See the description of the
 * set_memory_* function for more details on conventions.
 *
 * These APIs should be considered *deprecated* and are likely going to
 * be removed in the future.
 * The reason for this is the implicit operation on the 1:1 mapping only,
 * making this not a generally useful API.
 *
 * Specifically, many users of the old APIs had a virtual address,
 * called virt_to_page() or vmalloc_to_page() on that address to
 * get a struct page* that the old API required.
 * To convert these cases, use set_memory_*() on the original
 * virtual address, do not use these functions.
 */

int set_pages_uc(struct page *page, int numpages);
int set_pages_wb(struct page *page, int numpages);
int set_pages_x(struct page *page, int numpages);
int set_pages_nx(struct page *page, int numpages);
int set_pages_ro(struct page *page, int numpages);
int set_pages_rw(struct page *page, int numpages);

int set_direct_map_invalid_noflush(struct page *page);
int set_direct_map_default_noflush(struct page *page);

extern int kernel_set_to_readonly;
void set_kernel_text_rw(void);
void set_kernel_text_ro(void);

#ifdef CONFIG_X86_64
static inline int set_mce_nospec(unsigned long pfn)
{
	unsigned long decoy_addr;
	int rc;

	/*
	 * Mark the linear address as UC to make sure we don't log more
	 * errors because of speculative access to the page.
	 * We would like to just call:
	 *      set_memory_uc((unsigned long)pfn_to_kaddr(pfn), 1);
	 * but doing that would radically increase the odds of a
	 * speculative access to the poison page because we'd have
	 * the virtual address of the kernel 1:1 mapping sitting
	 * around in registers.
	 * Instead we get tricky.  We create a non-canonical address
	 * that looks just like the one we want, but has bit 63 flipped.
	 * This relies on set_memory_uc() properly sanitizing any __pa()
	 * results with __PHYSICAL_MASK or PTE_PFN_MASK.
	 */
	decoy_addr = (pfn << PAGE_SHIFT) + (PAGE_OFFSET ^ BIT(63));

	rc = set_memory_uc(decoy_addr, 1);
	if (rc)
		pr_warn("Could not invalidate pfn=0x%lx from 1:1 map\n", pfn);
	return rc;
}
#define set_mce_nospec set_mce_nospec

/* Restore full speculative operation to the pfn. */
static inline int clear_mce_nospec(unsigned long pfn)
{
	return set_memory_wb((unsigned long) pfn_to_kaddr(pfn), 1);
}
#define clear_mce_nospec clear_mce_nospec
#else
/*
 * Few people would run a 32-bit kernel on a machine that supports
 * recoverable errors because they have too much memory to boot 32-bit.
 */
#endif

#endif /* _ASM_X86_SET_MEMORY_H */
