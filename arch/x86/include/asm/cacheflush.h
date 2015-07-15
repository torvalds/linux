#ifndef _ASM_X86_CACHEFLUSH_H
#define _ASM_X86_CACHEFLUSH_H

/* Caches aren't brain-dead on the intel. */
#include <asm-generic/cacheflush.h>
#include <asm/special_insns.h>
#include <asm/uaccess.h>

/*
 * The set_memory_* API can be used to change various attributes of a virtual
 * address range. The attributes include:
 * Cachability   : UnCached, WriteCombining, WriteThrough, WriteBack
 * Executability : eXeutable, NoteXecutable
 * Read/Write    : ReadOnly, ReadWrite
 * Presence      : NotPresent
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
int set_memory_x(unsigned long addr, int numpages);
int set_memory_nx(unsigned long addr, int numpages);
int set_memory_ro(unsigned long addr, int numpages);
int set_memory_rw(unsigned long addr, int numpages);
int set_memory_np(unsigned long addr, int numpages);
int set_memory_4k(unsigned long addr, int numpages);

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


void clflush_cache_range(void *addr, unsigned int size);

#ifdef CONFIG_DEBUG_RODATA
void mark_rodata_ro(void);
extern const int rodata_test_data;
extern int kernel_set_to_readonly;
void set_kernel_text_rw(void);
void set_kernel_text_ro(void);
#else
static inline void set_kernel_text_rw(void) { }
static inline void set_kernel_text_ro(void) { }
#endif

#ifdef CONFIG_DEBUG_RODATA_TEST
int rodata_test(void);
#else
static inline int rodata_test(void)
{
	return 0;
}
#endif

#ifdef ARCH_HAS_NOCACHE_UACCESS

/**
 * arch_memcpy_to_pmem - copy data to persistent memory
 * @dst: destination buffer for the copy
 * @src: source buffer for the copy
 * @n: length of the copy in bytes
 *
 * Copy data to persistent memory media via non-temporal stores so that
 * a subsequent arch_wmb_pmem() can flush cpu and memory controller
 * write buffers to guarantee durability.
 */
static inline void arch_memcpy_to_pmem(void __pmem *dst, const void *src,
		size_t n)
{
	int unwritten;

	/*
	 * We are copying between two kernel buffers, if
	 * __copy_from_user_inatomic_nocache() returns an error (page
	 * fault) we would have already reported a general protection fault
	 * before the WARN+BUG.
	 */
	unwritten = __copy_from_user_inatomic_nocache((void __force *) dst,
			(void __user *) src, n);
	if (WARN(unwritten, "%s: fault copying %p <- %p unwritten: %d\n",
				__func__, dst, src, unwritten))
		BUG();
}

/**
 * arch_wmb_pmem - synchronize writes to persistent memory
 *
 * After a series of arch_memcpy_to_pmem() operations this drains data
 * from cpu write buffers and any platform (memory controller) buffers
 * to ensure that written data is durable on persistent memory media.
 */
static inline void arch_wmb_pmem(void)
{
	/*
	 * wmb() to 'sfence' all previous writes such that they are
	 * architecturally visible to 'pcommit'.  Note, that we've
	 * already arranged for pmem writes to avoid the cache via
	 * arch_memcpy_to_pmem().
	 */
	wmb();
	pcommit_sfence();
}

static inline bool __arch_has_wmb_pmem(void)
{
#ifdef CONFIG_X86_64
	/*
	 * We require that wmb() be an 'sfence', that is only guaranteed on
	 * 64-bit builds
	 */
	return static_cpu_has(X86_FEATURE_PCOMMIT);
#else
	return false;
#endif
}
#else /* ARCH_HAS_NOCACHE_UACCESS i.e. ARCH=um */
extern void arch_memcpy_to_pmem(void __pmem *dst, const void *src, size_t n);
extern void arch_wmb_pmem(void);

static inline bool __arch_has_wmb_pmem(void)
{
	return false;
}
#endif

#endif /* _ASM_X86_CACHEFLUSH_H */
