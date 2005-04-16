/*
 *  linux/arch/m68knommu/mm/memory.c
 *
 *  Copyright (C) 1998  Kenneth Albanowski <kjahds@kjahds.com>,
 *  Copyright (C) 1999-2002, Greg Ungerer (gerg@snapgear.com)
 *
 *  Based on:
 *
 *  linux/arch/m68k/mm/memory.c
 *
 *  Copyright (C) 1995  Hamish Macdonald
 */

#include <linux/config.h>
#include <linux/mm.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/slab.h>

#include <asm/setup.h>
#include <asm/segment.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/system.h>
#include <asm/traps.h>
#include <asm/io.h>

/*
 * cache_clear() semantics: Clear any cache entries for the area in question,
 * without writing back dirty entries first. This is useful if the data will
 * be overwritten anyway, e.g. by DMA to memory. The range is defined by a
 * _physical_ address.
 */

void cache_clear (unsigned long paddr, int len)
{
}


/*
 *	Define cache invalidate functions. The ColdFire 5407 is really
 *	the only processor that needs to do some work here. Anything
 *	that has separate data and instruction caches will be a problem.
 */
#ifdef CONFIG_M5407

static __inline__ void cache_invalidate_lines(unsigned long paddr, int len)
{
	unsigned long	sset, eset;

	sset = (paddr & 0x00000ff0);
	eset = ((paddr + len) & 0x0000ff0) + 0x10;

	__asm__ __volatile__ (
		"nop\n\t"
		"clrl	%%d0\n\t"
		"1:\n\t"
		"movel	%0,%%a0\n\t"
		"addl	%%d0,%%a0\n\t"
		"2:\n\t"
		".word	0xf4e8\n\t"
		"addl	#0x10,%%a0\n\t"
		"cmpl	%1,%%a0\n\t"
		"blt	2b\n\t"
		"addql	#1,%%d0\n\t"
		"cmpil	#4,%%d0\n\t"
		"bne	1b"
		: : "a" (sset), "a" (eset) : "d0", "a0" );
}

#else
#define	cache_invalidate_lines(a,b)
#endif


/*
 * cache_push() semantics: Write back any dirty cache data in the given area,
 * and invalidate the range in the instruction cache. It needs not (but may)
 * invalidate those entries also in the data cache. The range is defined by a
 * _physical_ address.
 */

void cache_push (unsigned long paddr, int len)
{
	cache_invalidate_lines(paddr, len);
}


/*
 * cache_push_v() semantics: Write back any dirty cache data in the given
 * area, and invalidate those entries at least in the instruction cache. This
 * is intended to be used after data has been written that can be executed as
 * code later. The range is defined by a _user_mode_ _virtual_ address  (or,
 * more exactly, the space is defined by the %sfc/%dfc register.)
 */

void cache_push_v (unsigned long vaddr, int len)
{
	cache_invalidate_lines(vaddr, len);
}

/* Map some physical address range into the kernel address space. The
 * code is copied and adapted from map_chunk().
 */

unsigned long kernel_map(unsigned long paddr, unsigned long size,
			 int nocacheflag, unsigned long *memavailp )
{
	return paddr;
}


int is_in_rom(unsigned long addr)
{
	extern unsigned long _ramstart, _ramend;

	/*
	 *	What we are really trying to do is determine if addr is
	 *	in an allocated kernel memory region. If not then assume
	 *	we cannot free it or otherwise de-allocate it. Ideally
	 *	we could restrict this to really being in a ROM or flash,
	 *	but that would need to be done on a board by board basis,
	 *	not globally.
	 */
	if ((addr < _ramstart) || (addr >= _ramend))
		return(1);

	/* Default case, not in ROM */
	return(0);
}

