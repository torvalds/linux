#ifndef _SPARC_CACHEFLUSH_H
#define _SPARC_CACHEFLUSH_H

#include <asm/cachetlb_32.h>

#define flush_cache_all() \
	sparc32_cachetlb_ops->cache_all()
#define flush_cache_mm(mm) \
	sparc32_cachetlb_ops->cache_mm(mm)
#define flush_cache_dup_mm(mm) \
	sparc32_cachetlb_ops->cache_mm(mm)
#define flush_cache_range(vma,start,end) \
	sparc32_cachetlb_ops->cache_range(vma, start, end)
#define flush_cache_page(vma,addr,pfn) \
	sparc32_cachetlb_ops->cache_page(vma, addr)
#define flush_icache_range(start, end)		do { } while (0)
#define flush_icache_page(vma, pg)		do { } while (0)

#define flush_icache_user_range(vma,pg,adr,len)	do { } while (0)

#define copy_to_user_page(vma, page, vaddr, dst, src, len) \
	do {							\
		flush_cache_page(vma, vaddr, page_to_pfn(page));\
		memcpy(dst, src, len);				\
	} while (0)
#define copy_from_user_page(vma, page, vaddr, dst, src, len) \
	do {							\
		flush_cache_page(vma, vaddr, page_to_pfn(page));\
		memcpy(dst, src, len);				\
	} while (0)

#define __flush_page_to_ram(addr) \
	sparc32_cachetlb_ops->page_to_ram(addr)
#define flush_sig_insns(mm,insn_addr) \
	sparc32_cachetlb_ops->sig_insns(mm, insn_addr)
#define flush_page_for_dma(addr) \
	sparc32_cachetlb_ops->page_for_dma(addr)

extern void sparc_flush_page_to_ram(struct page *page);

#define ARCH_IMPLEMENTS_FLUSH_DCACHE_PAGE 1
#define flush_dcache_page(page)			sparc_flush_page_to_ram(page)
#define flush_dcache_mmap_lock(mapping)		do { } while (0)
#define flush_dcache_mmap_unlock(mapping)	do { } while (0)

#define flush_cache_vmap(start, end)		flush_cache_all()
#define flush_cache_vunmap(start, end)		flush_cache_all()

/* When a context switch happens we must flush all user windows so that
 * the windows of the current process are flushed onto its stack. This
 * way the windows are all clean for the next process and the stack
 * frames are up to date.
 */
extern void flush_user_windows(void);
extern void kill_user_windows(void);
extern void flushw_all(void);

#endif /* _SPARC_CACHEFLUSH_H */
