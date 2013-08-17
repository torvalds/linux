/*
 * viking.h:  Defines specific to the GNU/Viking MBUS module.
 *            This is SRMMU stuff.
 *
 * Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 */
#ifndef _SPARC_VIKING_H
#define _SPARC_VIKING_H

#include <asm/asi.h>
#include <asm/mxcc.h>
#include <asm/pgtsrmmu.h>

/* Bits in the SRMMU control register for GNU/Viking modules.
 *
 * -----------------------------------------------------------
 * |impl-vers| RSV |TC|AC|SP|BM|PC|MBM|SB|IC|DC|PSO|RSV|NF|ME|
 * -----------------------------------------------------------
 *  31     24 23-17 16 15 14 13 12 11  10  9  8  7  6-2  1  0
 *
 * TC: Tablewalk Cacheable -- 0 = Twalks are not cacheable in E-cache
 *                            1 = Twalks are cacheable in E-cache
 *
 * GNU/Viking will only cache tablewalks in the E-cache (mxcc) if present
 * and never caches them internally (or so states the docs).  Therefore
 * for machines lacking an E-cache (ie. in MBUS mode) this bit must
 * remain cleared.
 *
 * AC: Alternate Cacheable -- 0 = Passthru physical accesses not cacheable
 *                            1 = Passthru physical accesses cacheable
 *
 * This indicates whether accesses are cacheable when no cachable bit
 * is present in the pte when the processor is in boot-mode or the
 * access does not need pte's for translation (ie. pass-thru ASI's).
 * "Cachable" is only referring to E-cache (if present) and not the
 * on chip split I/D caches of the GNU/Viking.
 *
 * SP: SnooP Enable -- 0 = bus snooping off, 1 = bus snooping on
 *
 * This enables snooping on the GNU/Viking bus.  This must be on
 * for the hardware cache consistency mechanisms of the GNU/Viking
 * to work at all.  On non-mxcc GNU/Viking modules the split I/D
 * caches will snoop regardless of whether they are enabled, this
 * takes care of the case where the I or D or both caches are turned
 * off yet still contain valid data.  Note also that this bit does
 * not affect GNU/Viking store-buffer snoops, those happen if the
 * store-buffer is enabled no matter what.
 *
 * BM: Boot Mode -- 0 = not in boot mode, 1 = in boot mode
 *
 * This indicates whether the GNU/Viking is in boot-mode or not,
 * if it is then all instruction fetch physical addresses are
 * computed as 0xff0000000 + low 28 bits of requested address.
 * GNU/Viking boot-mode does not affect data accesses.  Also,
 * in boot mode instruction accesses bypass the split on chip I/D
 * caches, they may be cached by the GNU/MXCC if present and enabled.
 *
 * MBM: MBus Mode -- 0 = not in MBus mode, 1 = in MBus mode
 *
 * This indicated the GNU/Viking configuration present.  If in
 * MBUS mode, the GNU/Viking lacks a GNU/MXCC E-cache.  If it is
 * not then the GNU/Viking is on a module VBUS connected directly
 * to a GNU/MXCC cache controller.  The GNU/MXCC can be thus connected
 * to either an GNU/MBUS (sun4m) or the packet-switched GNU/XBus (sun4d).
 *
 * SB: StoreBuffer enable -- 0 = store buffer off, 1 = store buffer on
 *
 * The GNU/Viking store buffer allows the chip to continue execution
 * after a store even if the data cannot be placed in one of the
 * caches during that cycle.  If disabled, all stores operations
 * occur synchronously.
 *
 * IC: Instruction Cache -- 0 = off, 1 = on
 * DC: Data Cache -- 0 = off, 1 = 0n
 *
 * These bits enable the on-cpu GNU/Viking split I/D caches.  Note,
 * as mentioned above, these caches will snoop the bus in GNU/MBUS
 * configurations even when disabled to avoid data corruption.
 *
 * NF: No Fault -- 0 = faults generate traps, 1 = faults don't trap
 * ME: MMU enable -- 0 = mmu not translating, 1 = mmu translating
 *
 */

#define VIKING_MMUENABLE    0x00000001
#define VIKING_NOFAULT      0x00000002
#define VIKING_PSO          0x00000080
#define VIKING_DCENABLE     0x00000100   /* Enable data cache */
#define VIKING_ICENABLE     0x00000200   /* Enable instruction cache */
#define VIKING_SBENABLE     0x00000400   /* Enable store buffer */
#define VIKING_MMODE        0x00000800   /* MBUS mode */
#define VIKING_PCENABLE     0x00001000   /* Enable parity checking */
#define VIKING_BMODE        0x00002000   
#define VIKING_SPENABLE     0x00004000   /* Enable bus cache snooping */
#define VIKING_ACENABLE     0x00008000   /* Enable alternate caching */
#define VIKING_TCENABLE     0x00010000   /* Enable table-walks to be cached */
#define VIKING_DPENABLE     0x00040000   /* Enable the data prefetcher */

/*
 * GNU/Viking Breakpoint Action Register fields.
 */
#define VIKING_ACTION_MIX   0x00001000   /* Enable multiple instructions */

/*
 * GNU/Viking Cache Tags.
 */
#define VIKING_PTAG_VALID   0x01000000   /* Cache block is valid */
#define VIKING_PTAG_DIRTY   0x00010000   /* Block has been modified */
#define VIKING_PTAG_SHARED  0x00000100   /* Shared with some other cache */

#ifndef __ASSEMBLY__

static inline void viking_flush_icache(void)
{
	__asm__ __volatile__("sta %%g0, [%%g0] %0\n\t"
			     : /* no outputs */
			     : "i" (ASI_M_IC_FLCLEAR)
			     : "memory");
}

static inline void viking_flush_dcache(void)
{
	__asm__ __volatile__("sta %%g0, [%%g0] %0\n\t"
			     : /* no outputs */
			     : "i" (ASI_M_DC_FLCLEAR)
			     : "memory");
}

static inline void viking_unlock_icache(void)
{
	__asm__ __volatile__("sta %%g0, [%0] %1\n\t"
			     : /* no outputs */
			     : "r" (0x80000000), "i" (ASI_M_IC_FLCLEAR)
			     : "memory");
}

static inline void viking_unlock_dcache(void)
{
	__asm__ __volatile__("sta %%g0, [%0] %1\n\t"
			     : /* no outputs */
			     : "r" (0x80000000), "i" (ASI_M_DC_FLCLEAR)
			     : "memory");
}

static inline void viking_set_bpreg(unsigned long regval)
{
	__asm__ __volatile__("sta %0, [%%g0] %1\n\t"
			     : /* no outputs */
			     : "r" (regval), "i" (ASI_M_ACTION)
			     : "memory");
}

static inline unsigned long viking_get_bpreg(void)
{
	unsigned long regval;

	__asm__ __volatile__("lda [%%g0] %1, %0\n\t"
			     : "=r" (regval)
			     : "i" (ASI_M_ACTION));
	return regval;
}

static inline void viking_get_dcache_ptag(int set, int block,
					      unsigned long *data)
{
	unsigned long ptag = ((set & 0x7f) << 5) | ((block & 0x3) << 26) |
			     0x80000000;
	unsigned long info, page;

	__asm__ __volatile__ ("ldda [%2] %3, %%g2\n\t"
			      "or %%g0, %%g2, %0\n\t"
			      "or %%g0, %%g3, %1\n\t"
			      : "=r" (info), "=r" (page)
			      : "r" (ptag), "i" (ASI_M_DATAC_TAG)
			      : "g2", "g3");
	data[0] = info;
	data[1] = page;
}

static inline void viking_mxcc_turn_off_parity(unsigned long *mregp,
						   unsigned long *mxcc_cregp)
{
	unsigned long mreg = *mregp;
	unsigned long mxcc_creg = *mxcc_cregp;

	mreg &= ~(VIKING_PCENABLE);
	mxcc_creg &= ~(MXCC_CTL_PARE);

	__asm__ __volatile__ ("set 1f, %%g2\n\t"
			      "andcc %%g2, 4, %%g0\n\t"
			      "bne 2f\n\t"
			      " nop\n"
			      "1:\n\t"
			      "sta %0, [%%g0] %3\n\t"
			      "sta %1, [%2] %4\n\t"
			      "b 1f\n\t"
			      " nop\n\t"
			      "nop\n"
			      "2:\n\t"
			      "sta %0, [%%g0] %3\n\t"
			      "sta %1, [%2] %4\n"
			      "1:\n\t"
			      : /* no output */
			      : "r" (mreg), "r" (mxcc_creg),
			        "r" (MXCC_CREG), "i" (ASI_M_MMUREGS),
			        "i" (ASI_M_MXCC)
			      : "g2", "memory", "cc");
	*mregp = mreg;
	*mxcc_cregp = mxcc_creg;
}

static inline unsigned long viking_hwprobe(unsigned long vaddr)
{
	unsigned long val;

	vaddr &= PAGE_MASK;
	/* Probe all MMU entries. */
	__asm__ __volatile__("lda [%1] %2, %0\n\t"
			     : "=r" (val)
			     : "r" (vaddr | 0x400), "i" (ASI_M_FLUSH_PROBE));
	if (!val)
		return 0;

	/* Probe region. */
	__asm__ __volatile__("lda [%1] %2, %0\n\t"
			     : "=r" (val)
			     : "r" (vaddr | 0x200), "i" (ASI_M_FLUSH_PROBE));
	if ((val & SRMMU_ET_MASK) == SRMMU_ET_PTE) {
		vaddr &= ~SRMMU_PGDIR_MASK;
		vaddr >>= PAGE_SHIFT;
		return val | (vaddr << 8);
	}

	/* Probe segment. */
	__asm__ __volatile__("lda [%1] %2, %0\n\t"
			     : "=r" (val)
			     : "r" (vaddr | 0x100), "i" (ASI_M_FLUSH_PROBE));
	if ((val & SRMMU_ET_MASK) == SRMMU_ET_PTE) {
		vaddr &= ~SRMMU_REAL_PMD_MASK;
		vaddr >>= PAGE_SHIFT;
		return val | (vaddr << 8);
	}

	/* Probe page. */
	__asm__ __volatile__("lda [%1] %2, %0\n\t"
			     : "=r" (val)
			     : "r" (vaddr), "i" (ASI_M_FLUSH_PROBE));
	return val;
}

#endif /* !__ASSEMBLY__ */

#endif /* !(_SPARC_VIKING_H) */
