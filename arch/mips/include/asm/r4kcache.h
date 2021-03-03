/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Inline assembly cache operations.
 *
 * Copyright (C) 1996 David S. Miller (davem@davemloft.net)
 * Copyright (C) 1997 - 2002 Ralf Baechle (ralf@gnu.org)
 * Copyright (C) 2004 Ralf Baechle (ralf@linux-mips.org)
 */
#ifndef _ASM_R4KCACHE_H
#define _ASM_R4KCACHE_H

#include <linux/stringify.h>

#include <asm/asm.h>
#include <asm/asm-eva.h>
#include <asm/cacheops.h>
#include <asm/compiler.h>
#include <asm/cpu-features.h>
#include <asm/cpu-type.h>
#include <asm/mipsmtregs.h>
#include <asm/mmzone.h>
#include <asm/unroll.h>

extern void (*r4k_blast_dcache)(void);
extern void (*r4k_blast_icache)(void);

/*
 * This macro return a properly sign-extended address suitable as base address
 * for indexed cache operations.  Two issues here:
 *
 *  - The MIPS32 and MIPS64 specs permit an implementation to directly derive
 *    the index bits from the virtual address.	This breaks with tradition
 *    set by the R4000.	 To keep unpleasant surprises from happening we pick
 *    an address in KSEG0 / CKSEG0.
 *  - We need a properly sign extended address for 64-bit code.	 To get away
 *    without ifdefs we let the compiler do it by a type cast.
 */
#define INDEX_BASE	CKSEG0

#define _cache_op(insn, op, addr)					\
	__asm__ __volatile__(						\
	"	.set	push					\n"	\
	"	.set	noreorder				\n"	\
	"	.set "MIPS_ISA_ARCH_LEVEL"			\n"	\
	"	" insn("%0", "%1") "				\n"	\
	"	.set	pop					\n"	\
	:								\
	: "i" (op), "R" (*(unsigned char *)(addr)))

#define cache_op(op, addr)						\
	_cache_op(kernel_cache, op, addr)

static inline void flush_icache_line_indexed(unsigned long addr)
{
	cache_op(Index_Invalidate_I, addr);
}

static inline void flush_dcache_line_indexed(unsigned long addr)
{
	cache_op(Index_Writeback_Inv_D, addr);
}

static inline void flush_scache_line_indexed(unsigned long addr)
{
	cache_op(Index_Writeback_Inv_SD, addr);
}

static inline void flush_icache_line(unsigned long addr)
{
	switch (boot_cpu_type()) {
	case CPU_LOONGSON2EF:
		cache_op(Hit_Invalidate_I_Loongson2, addr);
		break;

	default:
		cache_op(Hit_Invalidate_I, addr);
		break;
	}
}

static inline void flush_dcache_line(unsigned long addr)
{
	cache_op(Hit_Writeback_Inv_D, addr);
}

static inline void invalidate_dcache_line(unsigned long addr)
{
	cache_op(Hit_Invalidate_D, addr);
}

static inline void invalidate_scache_line(unsigned long addr)
{
	cache_op(Hit_Invalidate_SD, addr);
}

static inline void flush_scache_line(unsigned long addr)
{
	cache_op(Hit_Writeback_Inv_SD, addr);
}

#ifdef CONFIG_EVA

#define protected_cache_op(op, addr)				\
({								\
	int __err = 0;						\
	__asm__ __volatile__(					\
	"	.set	push			\n"		\
	"	.set	noreorder		\n"		\
	"	.set	mips0			\n"		\
	"	.set	eva			\n"		\
	"1:	cachee	%1, (%2)		\n"		\
	"2:	.insn				\n"		\
	"	.set	pop			\n"		\
	"	.section .fixup,\"ax\"		\n"		\
	"3:	li	%0, %3			\n"		\
	"	j	2b			\n"		\
	"	.previous			\n"		\
	"	.section __ex_table,\"a\"	\n"		\
	"	"STR(PTR)" 1b, 3b		\n"		\
	"	.previous"					\
	: "+r" (__err)						\
	: "i" (op), "r" (addr), "i" (-EFAULT));			\
	__err;							\
})
#else

#define protected_cache_op(op, addr)				\
({								\
	int __err = 0;						\
	__asm__ __volatile__(					\
	"	.set	push			\n"		\
	"	.set	noreorder		\n"		\
	"	.set "MIPS_ISA_ARCH_LEVEL"	\n"		\
	"1:	cache	%1, (%2)		\n"		\
	"2:	.insn				\n"		\
	"	.set	pop			\n"		\
	"	.section .fixup,\"ax\"		\n"		\
	"3:	li	%0, %3			\n"		\
	"	j	2b			\n"		\
	"	.previous			\n"		\
	"	.section __ex_table,\"a\"	\n"		\
	"	"STR(PTR)" 1b, 3b		\n"		\
	"	.previous"					\
	: "+r" (__err)						\
	: "i" (op), "r" (addr), "i" (-EFAULT));			\
	__err;							\
})
#endif

/*
 * The next two are for badland addresses like signal trampolines.
 */
static inline int protected_flush_icache_line(unsigned long addr)
{
	switch (boot_cpu_type()) {
	case CPU_LOONGSON2EF:
		return protected_cache_op(Hit_Invalidate_I_Loongson2, addr);

	default:
		return protected_cache_op(Hit_Invalidate_I, addr);
	}
}

/*
 * R10000 / R12000 hazard - these processors don't support the Hit_Writeback_D
 * cacheop so we use Hit_Writeback_Inv_D which is supported by all R4000-style
 * caches.  We're talking about one cacheline unnecessarily getting invalidated
 * here so the penalty isn't overly hard.
 */
static inline int protected_writeback_dcache_line(unsigned long addr)
{
	return protected_cache_op(Hit_Writeback_Inv_D, addr);
}

static inline int protected_writeback_scache_line(unsigned long addr)
{
	return protected_cache_op(Hit_Writeback_Inv_SD, addr);
}

/*
 * This one is RM7000-specific
 */
static inline void invalidate_tcache_page(unsigned long addr)
{
	cache_op(Page_Invalidate_T, addr);
}

#define cache_unroll(times, insn, op, addr, lsize) do {			\
	int i = 0;							\
	unroll(times, _cache_op, insn, op, (addr) + (i++ * (lsize)));	\
} while (0)

/* build blast_xxx, blast_xxx_page, blast_xxx_page_indexed */
#define __BUILD_BLAST_CACHE(pfx, desc, indexop, hitop, lsize, extra)	\
static inline void extra##blast_##pfx##cache##lsize(void)		\
{									\
	unsigned long start = INDEX_BASE;				\
	unsigned long end = start + current_cpu_data.desc.waysize;	\
	unsigned long ws_inc = 1UL << current_cpu_data.desc.waybit;	\
	unsigned long ws_end = current_cpu_data.desc.ways <<		\
			       current_cpu_data.desc.waybit;		\
	unsigned long ws, addr;						\
									\
	for (ws = 0; ws < ws_end; ws += ws_inc)				\
		for (addr = start; addr < end; addr += lsize * 32)	\
			cache_unroll(32, kernel_cache, indexop,		\
				     addr | ws, lsize);			\
}									\
									\
static inline void extra##blast_##pfx##cache##lsize##_page(unsigned long page) \
{									\
	unsigned long start = page;					\
	unsigned long end = page + PAGE_SIZE;				\
									\
	do {								\
		cache_unroll(32, kernel_cache, hitop, start, lsize);	\
		start += lsize * 32;					\
	} while (start < end);						\
}									\
									\
static inline void extra##blast_##pfx##cache##lsize##_page_indexed(unsigned long page) \
{									\
	unsigned long indexmask = current_cpu_data.desc.waysize - 1;	\
	unsigned long start = INDEX_BASE + (page & indexmask);		\
	unsigned long end = start + PAGE_SIZE;				\
	unsigned long ws_inc = 1UL << current_cpu_data.desc.waybit;	\
	unsigned long ws_end = current_cpu_data.desc.ways <<		\
			       current_cpu_data.desc.waybit;		\
	unsigned long ws, addr;						\
									\
	for (ws = 0; ws < ws_end; ws += ws_inc)				\
		for (addr = start; addr < end; addr += lsize * 32)	\
			cache_unroll(32, kernel_cache, indexop,		\
				     addr | ws, lsize);			\
}

__BUILD_BLAST_CACHE(d, dcache, Index_Writeback_Inv_D, Hit_Writeback_Inv_D, 16, )
__BUILD_BLAST_CACHE(i, icache, Index_Invalidate_I, Hit_Invalidate_I, 16, )
__BUILD_BLAST_CACHE(s, scache, Index_Writeback_Inv_SD, Hit_Writeback_Inv_SD, 16, )
__BUILD_BLAST_CACHE(d, dcache, Index_Writeback_Inv_D, Hit_Writeback_Inv_D, 32, )
__BUILD_BLAST_CACHE(i, icache, Index_Invalidate_I, Hit_Invalidate_I, 32, )
__BUILD_BLAST_CACHE(i, icache, Index_Invalidate_I, Hit_Invalidate_I_Loongson2, 32, loongson2_)
__BUILD_BLAST_CACHE(s, scache, Index_Writeback_Inv_SD, Hit_Writeback_Inv_SD, 32, )
__BUILD_BLAST_CACHE(d, dcache, Index_Writeback_Inv_D, Hit_Writeback_Inv_D, 64, )
__BUILD_BLAST_CACHE(i, icache, Index_Invalidate_I, Hit_Invalidate_I, 64, )
__BUILD_BLAST_CACHE(s, scache, Index_Writeback_Inv_SD, Hit_Writeback_Inv_SD, 64, )
__BUILD_BLAST_CACHE(d, dcache, Index_Writeback_Inv_D, Hit_Writeback_Inv_D, 128, )
__BUILD_BLAST_CACHE(i, icache, Index_Invalidate_I, Hit_Invalidate_I, 128, )
__BUILD_BLAST_CACHE(s, scache, Index_Writeback_Inv_SD, Hit_Writeback_Inv_SD, 128, )

__BUILD_BLAST_CACHE(inv_d, dcache, Index_Writeback_Inv_D, Hit_Invalidate_D, 16, )
__BUILD_BLAST_CACHE(inv_d, dcache, Index_Writeback_Inv_D, Hit_Invalidate_D, 32, )
__BUILD_BLAST_CACHE(inv_s, scache, Index_Writeback_Inv_SD, Hit_Invalidate_SD, 16, )
__BUILD_BLAST_CACHE(inv_s, scache, Index_Writeback_Inv_SD, Hit_Invalidate_SD, 32, )
__BUILD_BLAST_CACHE(inv_s, scache, Index_Writeback_Inv_SD, Hit_Invalidate_SD, 64, )
__BUILD_BLAST_CACHE(inv_s, scache, Index_Writeback_Inv_SD, Hit_Invalidate_SD, 128, )

#define __BUILD_BLAST_USER_CACHE(pfx, desc, indexop, hitop, lsize) \
static inline void blast_##pfx##cache##lsize##_user_page(unsigned long page) \
{									\
	unsigned long start = page;					\
	unsigned long end = page + PAGE_SIZE;				\
									\
	do {								\
		cache_unroll(32, user_cache, hitop, start, lsize);	\
		start += lsize * 32;					\
	} while (start < end);						\
}

__BUILD_BLAST_USER_CACHE(d, dcache, Index_Writeback_Inv_D, Hit_Writeback_Inv_D,
			 16)
__BUILD_BLAST_USER_CACHE(i, icache, Index_Invalidate_I, Hit_Invalidate_I, 16)
__BUILD_BLAST_USER_CACHE(d, dcache, Index_Writeback_Inv_D, Hit_Writeback_Inv_D,
			 32)
__BUILD_BLAST_USER_CACHE(i, icache, Index_Invalidate_I, Hit_Invalidate_I, 32)
__BUILD_BLAST_USER_CACHE(d, dcache, Index_Writeback_Inv_D, Hit_Writeback_Inv_D,
			 64)
__BUILD_BLAST_USER_CACHE(i, icache, Index_Invalidate_I, Hit_Invalidate_I, 64)

/* build blast_xxx_range, protected_blast_xxx_range */
#define __BUILD_BLAST_CACHE_RANGE(pfx, desc, hitop, prot, extra)	\
static inline void prot##extra##blast_##pfx##cache##_range(unsigned long start, \
						    unsigned long end)	\
{									\
	unsigned long lsize = cpu_##desc##_line_size();			\
	unsigned long addr = start & ~(lsize - 1);			\
	unsigned long aend = (end - 1) & ~(lsize - 1);			\
									\
	while (1) {							\
		prot##cache_op(hitop, addr);				\
		if (addr == aend)					\
			break;						\
		addr += lsize;						\
	}								\
}

__BUILD_BLAST_CACHE_RANGE(d, dcache, Hit_Writeback_Inv_D, protected_, )
__BUILD_BLAST_CACHE_RANGE(i, icache, Hit_Invalidate_I, protected_, )
__BUILD_BLAST_CACHE_RANGE(s, scache, Hit_Writeback_Inv_SD, protected_, )
__BUILD_BLAST_CACHE_RANGE(i, icache, Hit_Invalidate_I_Loongson2, \
	protected_, loongson2_)
__BUILD_BLAST_CACHE_RANGE(d, dcache, Hit_Writeback_Inv_D, , )
__BUILD_BLAST_CACHE_RANGE(i, icache, Hit_Invalidate_I, , )
__BUILD_BLAST_CACHE_RANGE(s, scache, Hit_Writeback_Inv_SD, , )
/* blast_inv_dcache_range */
__BUILD_BLAST_CACHE_RANGE(inv_d, dcache, Hit_Invalidate_D, , )
__BUILD_BLAST_CACHE_RANGE(inv_s, scache, Hit_Invalidate_SD, , )

/* Currently, this is very specific to Loongson-3 */
#define __BUILD_BLAST_CACHE_NODE(pfx, desc, indexop, hitop, lsize)	\
static inline void blast_##pfx##cache##lsize##_node(long node)		\
{									\
	unsigned long start = CAC_BASE | nid_to_addrbase(node);		\
	unsigned long end = start + current_cpu_data.desc.waysize;	\
	unsigned long ws_inc = 1UL << current_cpu_data.desc.waybit;	\
	unsigned long ws_end = current_cpu_data.desc.ways <<		\
			       current_cpu_data.desc.waybit;		\
	unsigned long ws, addr;						\
									\
	for (ws = 0; ws < ws_end; ws += ws_inc)				\
		for (addr = start; addr < end; addr += lsize * 32)	\
			cache_unroll(32, kernel_cache, indexop,		\
				     addr | ws, lsize);			\
}

__BUILD_BLAST_CACHE_NODE(s, scache, Index_Writeback_Inv_SD, Hit_Writeback_Inv_SD, 16)
__BUILD_BLAST_CACHE_NODE(s, scache, Index_Writeback_Inv_SD, Hit_Writeback_Inv_SD, 32)
__BUILD_BLAST_CACHE_NODE(s, scache, Index_Writeback_Inv_SD, Hit_Writeback_Inv_SD, 64)
__BUILD_BLAST_CACHE_NODE(s, scache, Index_Writeback_Inv_SD, Hit_Writeback_Inv_SD, 128)

#endif /* _ASM_R4KCACHE_H */
