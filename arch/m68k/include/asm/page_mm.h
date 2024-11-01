/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _M68K_PAGE_MM_H
#define _M68K_PAGE_MM_H

#ifndef __ASSEMBLY__

#include <linux/compiler.h>
#include <asm/module.h>

/*
 * We don't need to check for alignment etc.
 */
#ifdef CPU_M68040_OR_M68060_ONLY
static inline void copy_page(void *to, void *from)
{
  unsigned long tmp;

  __asm__ __volatile__("1:\t"
		       ".chip 68040\n\t"
		       "move16 %1@+,%0@+\n\t"
		       "move16 %1@+,%0@+\n\t"
		       ".chip 68k\n\t"
		       "dbra  %2,1b\n\t"
		       : "=a" (to), "=a" (from), "=d" (tmp)
		       : "0" (to), "1" (from) , "2" (PAGE_SIZE / 32 - 1)
		       );
}

static inline void clear_page(void *page)
{
	unsigned long tmp;
	unsigned long *sp = page;

	*sp++ = 0;
	*sp++ = 0;
	*sp++ = 0;
	*sp++ = 0;

	__asm__ __volatile__("1:\t"
			     ".chip 68040\n\t"
			     "move16 %2@+,%0@+\n\t"
			     ".chip 68k\n\t"
			     "subqw  #8,%2\n\t"
			     "subqw  #8,%2\n\t"
			     "dbra   %1,1b\n\t"
			     : "=a" (sp), "=d" (tmp)
			     : "a" (page), "0" (sp),
			       "1" ((PAGE_SIZE - 16) / 16 - 1));
}

#else
#define clear_page(page)	memset((page), 0, PAGE_SIZE)
#define copy_page(to,from)	memcpy((to), (from), PAGE_SIZE)
#endif

#define clear_user_page(addr, vaddr, page)	\
	do {	clear_page(addr);		\
		flush_dcache_page(page);	\
	} while (0)
#define copy_user_page(to, from, vaddr, page)	\
	do {	copy_page(to, from);		\
		flush_dcache_page(page);	\
	} while (0)

extern unsigned long m68k_memoffset;

#ifndef CONFIG_SUN3

#define WANT_PAGE_VIRTUAL

static inline unsigned long ___pa(void *vaddr)
{
	unsigned long paddr;
	asm (
		"1:	addl #0,%0\n"
		m68k_fixup(%c2, 1b+2)
		: "=r" (paddr)
		: "0" (vaddr), "i" (m68k_fixup_memoffset));
	return paddr;
}
#define __pa(vaddr)	___pa((void *)(long)(vaddr))
static inline void *__va(unsigned long paddr)
{
	void *vaddr;
	asm (
		"1:	subl #0,%0\n"
		m68k_fixup(%c2, 1b+2)
		: "=r" (vaddr)
		: "0" (paddr), "i" (m68k_fixup_memoffset));
	return vaddr;
}

#else	/* !CONFIG_SUN3 */
/* This #define is a horrible hack to suppress lots of warnings. --m */
#define __pa(x) ___pa((unsigned long)(x))
static inline unsigned long ___pa(unsigned long x)
{
     if(x == 0)
	  return 0;
     if(x >= PAGE_OFFSET)
        return (x-PAGE_OFFSET);
     else
        return (x+0x2000000);
}

static inline void *__va(unsigned long x)
{
     if(x == 0)
	  return (void *)0;

     if(x < 0x2000000)
        return (void *)(x+PAGE_OFFSET);
     else
        return (void *)(x-0x2000000);
}
#endif	/* CONFIG_SUN3 */

/*
 * NOTE: virtual isn't really correct, actually it should be the offset into the
 * memory node, but we have no highmem, so that works for now.
 * TODO: implement (fast) pfn<->pgdat_idx conversion functions, this makes lots
 * of the shifts unnecessary.
 */
#define virt_to_pfn(kaddr)	(__pa(kaddr) >> PAGE_SHIFT)
#define pfn_to_virt(pfn)	__va((pfn) << PAGE_SHIFT)

extern int m68k_virt_to_node_shift;

#define virt_to_page(addr) ({						\
	pfn_to_page(virt_to_pfn(addr));					\
})
#define page_to_virt(page) ({						\
	pfn_to_virt(page_to_pfn(page));					\
})

#define ARCH_PFN_OFFSET (m68k_memory[0].addr >> PAGE_SHIFT)
#include <asm-generic/memory_model.h>

#define virt_addr_valid(kaddr)	((unsigned long)(kaddr) >= PAGE_OFFSET && (unsigned long)(kaddr) < (unsigned long)high_memory)
#define pfn_valid(pfn)		virt_addr_valid(pfn_to_virt(pfn))

#endif /* __ASSEMBLY__ */

#endif /* _M68K_PAGE_MM_H */
