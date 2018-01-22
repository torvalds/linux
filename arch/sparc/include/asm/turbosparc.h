/* SPDX-License-Identifier: GPL-2.0 */
/*
 * turbosparc.h:  Defines specific to the TurboSparc module.
 *            This is SRMMU stuff.
 *
 * Copyright (C) 1997 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 */
#ifndef _SPARC_TURBOSPARC_H
#define _SPARC_TURBOSPARC_H

#include <asm/asi.h>
#include <asm/pgtsrmmu.h>

/* Bits in the SRMMU control register for TurboSparc modules.
 *
 * -------------------------------------------------------------------
 * |impl-vers| RSV| PMC |PE|PC| RSV |BM| RFR |IC|DC|PSO|RSV|ICS|NF|ME|
 * -------------------------------------------------------------------
 *  31    24 23-21 20-19 18 17 16-15 14 13-10  9  8  7  6-3   2  1  0
 *
 * BM: Boot Mode -- 0 = not in boot mode, 1 = in boot mode
 *
 * This indicates whether the TurboSparc is in boot-mode or not.
 *
 * IC: Instruction Cache -- 0 = off, 1 = on
 * DC: Data Cache -- 0 = off, 1 = 0n
 *
 * These bits enable the on-cpu TurboSparc split I/D caches.
 *
 * ICS: ICache Snooping -- 0 = disable, 1 = enable snooping of icache
 * NF: No Fault -- 0 = faults generate traps, 1 = faults don't trap
 * ME: MMU enable -- 0 = mmu not translating, 1 = mmu translating
 *
 */

#define TURBOSPARC_MMUENABLE    0x00000001
#define TURBOSPARC_NOFAULT      0x00000002
#define TURBOSPARC_ICSNOOP	0x00000004
#define TURBOSPARC_PSO          0x00000080
#define TURBOSPARC_DCENABLE     0x00000100   /* Enable data cache */
#define TURBOSPARC_ICENABLE     0x00000200   /* Enable instruction cache */
#define TURBOSPARC_BMODE        0x00004000   
#define TURBOSPARC_PARITYODD	0x00020000   /* Parity odd, if enabled */
#define TURBOSPARC_PCENABLE	0x00040000   /* Enable parity checking */

/* Bits in the CPU configuration register for TurboSparc modules.
 *
 * -------------------------------------------------------
 * |IOClk|SNP|AXClk| RAH |  WS |  RSV  |SBC|WT|uS2|SE|SCC|
 * -------------------------------------------------------
 *    31   30 29-28 27-26 25-23   22-8  7-6  5  4   3 2-0
 *
 */

#define TURBOSPARC_SCENABLE 0x00000008	 /* Secondary cache enable */
#define TURBOSPARC_uS2	    0x00000010   /* Swift compatibility mode */
#define TURBOSPARC_WTENABLE 0x00000020	 /* Write thru for dcache */
#define TURBOSPARC_SNENABLE 0x40000000	 /* DVMA snoop enable */

#ifndef __ASSEMBLY__

/* Bits [13:5] select one of 512 instruction cache tags */
static inline void turbosparc_inv_insn_tag(unsigned long addr)
{
        __asm__ __volatile__("sta %%g0, [%0] %1\n\t"
			     : /* no outputs */
			     : "r" (addr), "i" (ASI_M_TXTC_TAG)
			     : "memory");
}

/* Bits [13:5] select one of 512 data cache tags */
static inline void turbosparc_inv_data_tag(unsigned long addr)
{
        __asm__ __volatile__("sta %%g0, [%0] %1\n\t"
			     : /* no outputs */
			     : "r" (addr), "i" (ASI_M_DATAC_TAG)
			     : "memory");
}

static inline void turbosparc_flush_icache(void)
{
	unsigned long addr;

        for (addr = 0; addr < 0x4000; addr += 0x20)
                turbosparc_inv_insn_tag(addr);
}

static inline void turbosparc_flush_dcache(void)
{
	unsigned long addr;

        for (addr = 0; addr < 0x4000; addr += 0x20)
                turbosparc_inv_data_tag(addr);
}

static inline void turbosparc_idflash_clear(void)
{
	unsigned long addr;

        for (addr = 0; addr < 0x4000; addr += 0x20) {
                turbosparc_inv_insn_tag(addr);
                turbosparc_inv_data_tag(addr);
	}
}

static inline void turbosparc_set_ccreg(unsigned long regval)
{
	__asm__ __volatile__("sta %0, [%1] %2\n\t"
			     : /* no outputs */
			     : "r" (regval), "r" (0x600), "i" (ASI_M_MMUREGS)
			     : "memory");
}

static inline unsigned long turbosparc_get_ccreg(void)
{
	unsigned long regval;

	__asm__ __volatile__("lda [%1] %2, %0\n\t"
			     : "=r" (regval)
			     : "r" (0x600), "i" (ASI_M_MMUREGS));
	return regval;
}

#endif /* !__ASSEMBLY__ */

#endif /* !(_SPARC_TURBOSPARC_H) */
