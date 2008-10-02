/*
 * ross.h: Ross module specific definitions and defines.
 *
 * Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 */

#ifndef _SPARC_ROSS_H
#define _SPARC_ROSS_H

#include <asm/asi.h>
#include <asm/page.h>

/* Ross made Hypersparcs have a %psr 'impl' field of '0001'.  The 'vers'
 * field has '1111'.
 */

/* The MMU control register fields on the HyperSparc.
 *
 * -----------------------------------------------------------------
 * |implvers| RSV |CWR|SE|WBE| MID |BM| C|CS|MR|CM|RSV|CE|RSV|NF|ME|
 * -----------------------------------------------------------------
 *  31    24 23-22 21  20  19 18-15 14 13 12 11 10  9   8 7-2  1  0
 *
 * Phew, lots of fields there ;-)
 *
 * CWR: Cache Wrapping Enabled, if one cache wrapping is on.
 * SE: Snoop Enable, turns on bus snooping for cache activity if one.
 * WBE: Write Buffer Enable, one turns it on.
 * MID: The ModuleID of the chip for MBus transactions.
 * BM: Boot-Mode. One indicates the MMU is in boot mode.
 * C: Indicates whether accesses are cachable while the MMU is
 *    disabled.
 * CS: Cache Size -- 0 = 128k, 1 = 256k
 * MR: Memory Reflection, one indicates that the memory bus connected
 *     to the MBus supports memory reflection.
 * CM: Cache Mode -- 0 = write-through, 1 = copy-back
 * CE: Cache Enable -- 0 = no caching, 1 = cache is on
 * NF: No Fault -- 0 = faults trap the CPU from supervisor mode
 *                 1 = faults from supervisor mode do not generate traps
 * ME: MMU Enable -- 0 = MMU is off, 1 = MMU is on
 */

#define HYPERSPARC_CWENABLE   0x00200000
#define HYPERSPARC_SBENABLE   0x00100000
#define HYPERSPARC_WBENABLE   0x00080000
#define HYPERSPARC_MIDMASK    0x00078000
#define HYPERSPARC_BMODE      0x00004000
#define HYPERSPARC_ACENABLE   0x00002000
#define HYPERSPARC_CSIZE      0x00001000
#define HYPERSPARC_MRFLCT     0x00000800
#define HYPERSPARC_CMODE      0x00000400
#define HYPERSPARC_CENABLE    0x00000100
#define HYPERSPARC_NFAULT     0x00000002
#define HYPERSPARC_MENABLE    0x00000001


/* The ICCR instruction cache register on the HyperSparc.
 *
 * -----------------------------------------------
 * |                                 | FTD | ICE |
 * -----------------------------------------------
 *  31                                  1     0
 *
 * This register is accessed using the V8 'wrasr' and 'rdasr'
 * opcodes, since not all assemblers understand them and those
 * that do use different semantics I will just hard code the
 * instruction with a '.word' statement.
 *
 * FTD:  If set to one flush instructions executed during an
 *       instruction cache hit occurs, the corresponding line
 *       for said cache-hit is invalidated.  If FTD is zero,
 *       an unimplemented 'flush' trap will occur when any
 *       flush is executed by the processor.
 *
 * ICE:  If set to one, the instruction cache is enabled.  If
 *       zero, the cache will not be used for instruction fetches.
 *
 * All other bits are read as zeros, and writes to them have no
 * effect.
 *
 * Wheee, not many assemblers understand the %iccr register nor
 * the generic asr r/w instructions.
 *
 *  1000 0011 0100 0111 1100 0000 0000 0000   ! rd %iccr, %g1
 *
 * 0x  8    3    4    7    c    0    0    0   ! 0x8347c000
 *
 *  1011 1111 1000 0000 0110 0000 0000 0000   ! wr %g1, 0x0, %iccr
 *
 * 0x  b    f    8    0    6    0    0    0   ! 0xbf806000
 *
 */

#define HYPERSPARC_ICCR_FTD     0x00000002
#define HYPERSPARC_ICCR_ICE     0x00000001

#ifndef __ASSEMBLY__

static inline unsigned int get_ross_icr(void)
{
	unsigned int icreg;

	__asm__ __volatile__(".word 0x8347c000\n\t" /* rd %iccr, %g1 */
			     "mov %%g1, %0\n\t"
			     : "=r" (icreg)
			     : /* no inputs */
			     : "g1", "memory");

	return icreg;
}

static inline void put_ross_icr(unsigned int icreg)
{
	__asm__ __volatile__("or %%g0, %0, %%g1\n\t"
			     ".word 0xbf806000\n\t" /* wr %g1, 0x0, %iccr */
			     "nop\n\t"
			     "nop\n\t"
			     "nop\n\t"
			     : /* no outputs */
			     : "r" (icreg)
			     : "g1", "memory");

	return;
}

/* HyperSparc specific cache flushing. */

/* This is for the on-chip instruction cache. */
static inline void hyper_flush_whole_icache(void)
{
	__asm__ __volatile__("sta %%g0, [%%g0] %0\n\t"
			     : /* no outputs */
			     : "i" (ASI_M_FLUSH_IWHOLE)
			     : "memory");
	return;
}

extern int vac_cache_size;
extern int vac_line_size;

static inline void hyper_clear_all_tags(void)
{
	unsigned long addr;

	for(addr = 0; addr < vac_cache_size; addr += vac_line_size)
		__asm__ __volatile__("sta %%g0, [%0] %1\n\t"
				     : /* no outputs */
				     : "r" (addr), "i" (ASI_M_DATAC_TAG)
				     : "memory");
}

static inline void hyper_flush_unconditional_combined(void)
{
	unsigned long addr;

	for (addr = 0; addr < vac_cache_size; addr += vac_line_size)
		__asm__ __volatile__("sta %%g0, [%0] %1\n\t"
				     : /* no outputs */
				     : "r" (addr), "i" (ASI_M_FLUSH_CTX)
				     : "memory");
}

static inline void hyper_flush_cache_user(void)
{
	unsigned long addr;

	for (addr = 0; addr < vac_cache_size; addr += vac_line_size)
		__asm__ __volatile__("sta %%g0, [%0] %1\n\t"
				     : /* no outputs */
				     : "r" (addr), "i" (ASI_M_FLUSH_USER)
				     : "memory");
}

static inline void hyper_flush_cache_page(unsigned long page)
{
	unsigned long end;

	page &= PAGE_MASK;
	end = page + PAGE_SIZE;
	while (page < end) {
		__asm__ __volatile__("sta %%g0, [%0] %1\n\t"
				     : /* no outputs */
				     : "r" (page), "i" (ASI_M_FLUSH_PAGE)
				     : "memory");
		page += vac_line_size;
	}
}

#endif /* !(__ASSEMBLY__) */

#endif /* !(_SPARC_ROSS_H) */
