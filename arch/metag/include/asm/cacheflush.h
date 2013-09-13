#ifndef _METAG_CACHEFLUSH_H
#define _METAG_CACHEFLUSH_H

#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/io.h>

#include <asm/l2cache.h>
#include <asm/metag_isa.h>
#include <asm/metag_mem.h>

void metag_cache_probe(void);

void metag_data_cache_flush_all(const void *start);
void metag_code_cache_flush_all(const void *start);

/*
 * Routines to flush physical cache lines that may be used to cache data or code
 * normally accessed via the linear address range supplied. The region flushed
 * must either lie in local or global address space determined by the top bit of
 * the pStart address. If Bytes is >= 4K then the whole of the related cache
 * state will be flushed rather than a limited range.
 */
void metag_data_cache_flush(const void *start, int bytes);
void metag_code_cache_flush(const void *start, int bytes);

#ifdef CONFIG_METAG_META12

/* Write through, virtually tagged, split I/D cache. */

static inline void __flush_cache_all(void)
{
	metag_code_cache_flush_all((void *) PAGE_OFFSET);
	metag_data_cache_flush_all((void *) PAGE_OFFSET);
}

#define flush_cache_all() __flush_cache_all()

/* flush the entire user address space referenced in this mm structure */
static inline void flush_cache_mm(struct mm_struct *mm)
{
	if (mm == current->mm)
		__flush_cache_all();
}

#define flush_cache_dup_mm(mm) flush_cache_mm(mm)

/* flush a range of addresses from this mm */
static inline void flush_cache_range(struct vm_area_struct *vma,
				     unsigned long start, unsigned long end)
{
	flush_cache_mm(vma->vm_mm);
}

static inline void flush_cache_page(struct vm_area_struct *vma,
				    unsigned long vmaddr, unsigned long pfn)
{
	flush_cache_mm(vma->vm_mm);
}

#define ARCH_IMPLEMENTS_FLUSH_DCACHE_PAGE	1
static inline void flush_dcache_page(struct page *page)
{
	metag_data_cache_flush_all((void *) PAGE_OFFSET);
}

#define flush_dcache_mmap_lock(mapping)		do { } while (0)
#define flush_dcache_mmap_unlock(mapping)	do { } while (0)

static inline void flush_icache_page(struct vm_area_struct *vma,
				     struct page *page)
{
	metag_code_cache_flush(page_to_virt(page), PAGE_SIZE);
}

static inline void flush_cache_vmap(unsigned long start, unsigned long end)
{
	metag_data_cache_flush_all((void *) PAGE_OFFSET);
}

static inline void flush_cache_vunmap(unsigned long start, unsigned long end)
{
	metag_data_cache_flush_all((void *) PAGE_OFFSET);
}

#else

/* Write through, physically tagged, split I/D cache. */

#define flush_cache_all()			do { } while (0)
#define flush_cache_mm(mm)			do { } while (0)
#define flush_cache_dup_mm(mm)			do { } while (0)
#define flush_cache_range(vma, start, end)	do { } while (0)
#define flush_cache_page(vma, vmaddr, pfn)	do { } while (0)
#define flush_dcache_mmap_lock(mapping)		do { } while (0)
#define flush_dcache_mmap_unlock(mapping)	do { } while (0)
#define flush_icache_page(vma, pg)		do { } while (0)
#define flush_cache_vmap(start, end)		do { } while (0)
#define flush_cache_vunmap(start, end)		do { } while (0)

#define ARCH_IMPLEMENTS_FLUSH_DCACHE_PAGE	1
static inline void flush_dcache_page(struct page *page)
{
	/* FIXME: We can do better than this. All we are trying to do is
	 * make the i-cache coherent, we should use the PG_arch_1 bit like
	 * e.g. powerpc.
	 */
#ifdef CONFIG_SMP
	metag_out32(1, SYSC_ICACHE_FLUSH);
#else
	metag_code_cache_flush_all((void *) PAGE_OFFSET);
#endif
}

#endif

/* Push n pages at kernel virtual address and clear the icache */
static inline void flush_icache_range(unsigned long address,
				      unsigned long endaddr)
{
#ifdef CONFIG_SMP
	metag_out32(1, SYSC_ICACHE_FLUSH);
#else
	metag_code_cache_flush((void *) address, endaddr - address);
#endif
}

static inline void flush_cache_sigtramp(unsigned long addr, int size)
{
	/*
	 * Flush the icache in case there was previously some code
	 * fetched from this address, perhaps a previous sigtramp.
	 *
	 * We don't need to flush the dcache, it's write through and
	 * we just wrote the sigtramp code through it.
	 */
#ifdef CONFIG_SMP
	metag_out32(1, SYSC_ICACHE_FLUSH);
#else
	metag_code_cache_flush((void *) addr, size);
#endif
}

#ifdef CONFIG_METAG_L2C

/*
 * Perform a single specific CACHEWD operation on an address, masking lower bits
 * of address first.
 */
static inline void cachewd_line(void *addr, unsigned int data)
{
	unsigned long masked = (unsigned long)addr & -0x40;
	__builtin_meta2_cachewd((void *)masked, data);
}

/* Perform a certain CACHEW op on each cache line in a range */
static inline void cachew_region_op(void *start, unsigned long size,
				    unsigned int op)
{
	unsigned long offset = (unsigned long)start & 0x3f;
	int i;
	if (offset) {
		size += offset;
		start -= offset;
	}
	i = (size - 1) >> 6;
	do {
		__builtin_meta2_cachewd(start, op);
		start += 0x40;
	} while (i--);
}

/* prevent write fence and flushbacks being reordered in L2 */
static inline void l2c_fence_flush(void *addr)
{
	/*
	 * Synchronise by reading back and re-flushing.
	 * It is assumed this access will miss, as the caller should have just
	 * flushed the cache line.
	 */
	(void)(volatile u8 *)addr;
	cachewd_line(addr, CACHEW_FLUSH_L1D_L2);
}

/* prevent write fence and writebacks being reordered in L2 */
static inline void l2c_fence(void *addr)
{
	/*
	 * A write back has occurred, but not necessarily an invalidate, so the
	 * readback in l2c_fence_flush() would hit in the cache and have no
	 * effect. Therefore fully flush the line first.
	 */
	cachewd_line(addr, CACHEW_FLUSH_L1D_L2);
	l2c_fence_flush(addr);
}

/* Used to keep memory consistent when doing DMA. */
static inline void flush_dcache_region(void *start, unsigned long size)
{
	/* metag_data_cache_flush won't flush L2 cache lines if size >= 4096 */
	if (meta_l2c_is_enabled()) {
		cachew_region_op(start, size, CACHEW_FLUSH_L1D_L2);
		if (meta_l2c_is_writeback())
			l2c_fence_flush(start + size - 1);
	} else {
		metag_data_cache_flush(start, size);
	}
}

/* Write back dirty lines to memory (or do nothing if no writeback caches) */
static inline void writeback_dcache_region(void *start, unsigned long size)
{
	if (meta_l2c_is_enabled() && meta_l2c_is_writeback()) {
		cachew_region_op(start, size, CACHEW_WRITEBACK_L1D_L2);
		l2c_fence(start + size - 1);
	}
}

/* Invalidate (may also write back if necessary) */
static inline void invalidate_dcache_region(void *start, unsigned long size)
{
	if (meta_l2c_is_enabled())
		cachew_region_op(start, size, CACHEW_INVALIDATE_L1D_L2);
	else
		metag_data_cache_flush(start, size);
}
#else
#define flush_dcache_region(s, l)	metag_data_cache_flush((s), (l))
#define writeback_dcache_region(s, l)	do {} while (0)
#define invalidate_dcache_region(s, l)	flush_dcache_region((s), (l))
#endif

static inline void copy_to_user_page(struct vm_area_struct *vma,
				     struct page *page, unsigned long vaddr,
				     void *dst, const void *src,
				     unsigned long len)
{
	memcpy(dst, src, len);
	flush_icache_range((unsigned long)dst, (unsigned long)dst + len);
}

static inline void copy_from_user_page(struct vm_area_struct *vma,
				       struct page *page, unsigned long vaddr,
				       void *dst, const void *src,
				       unsigned long len)
{
	memcpy(dst, src, len);
}

#endif /* _METAG_CACHEFLUSH_H */
