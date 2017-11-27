/* SPDX-License-Identifier: GPL-2.0 */
/* swift.h: Specific definitions for the _broken_ Swift SRMMU
 *          MMU module.
 *
 * Copyright (C) 1996 David S. Miller (davem@caip.rutgers.edu)
 */

#ifndef _SPARC_SWIFT_H
#define _SPARC_SWIFT_H

/* Swift is so brain damaged, here is the mmu control register. */
#define SWIFT_ST       0x00800000   /* SW tablewalk enable */
#define SWIFT_WP       0x00400000   /* Watchpoint enable   */

/* Branch folding (buggy, disable on production systems!)  */
#define SWIFT_BF       0x00200000
#define SWIFT_PMC      0x00180000   /* Page mode control   */
#define SWIFT_PE       0x00040000   /* Parity enable       */
#define SWIFT_PC       0x00020000   /* Parity control      */
#define SWIFT_AP       0x00010000   /* Graphics page mode control (TCX/SX) */
#define SWIFT_AC       0x00008000   /* Alternate Cacheability (see viking.h) */
#define SWIFT_BM       0x00004000   /* Boot mode */
#define SWIFT_RC       0x00003c00   /* DRAM refresh control */
#define SWIFT_IE       0x00000200   /* Instruction cache enable */
#define SWIFT_DE       0x00000100   /* Data cache enable */
#define SWIFT_SA       0x00000080   /* Store Allocate */
#define SWIFT_NF       0x00000002   /* No fault mode */
#define SWIFT_EN       0x00000001   /* MMU enable */

/* Bits [13:5] select one of 512 instruction cache tags */
static inline void swift_inv_insn_tag(unsigned long addr)
{
	__asm__ __volatile__("sta %%g0, [%0] %1\n\t"
			     : /* no outputs */
			     : "r" (addr), "i" (ASI_M_TXTC_TAG)
			     : "memory");
}

/* Bits [12:4] select one of 512 data cache tags */
static inline void swift_inv_data_tag(unsigned long addr)
{
	__asm__ __volatile__("sta %%g0, [%0] %1\n\t"
			     : /* no outputs */
			     : "r" (addr), "i" (ASI_M_DATAC_TAG)
			     : "memory");
}

static inline void swift_flush_dcache(void)
{
	unsigned long addr;

	for (addr = 0; addr < 0x2000; addr += 0x10)
		swift_inv_data_tag(addr);
}

static inline void swift_flush_icache(void)
{
	unsigned long addr;

	for (addr = 0; addr < 0x4000; addr += 0x20)
		swift_inv_insn_tag(addr);
}

static inline void swift_idflash_clear(void)
{
	unsigned long addr;

	for (addr = 0; addr < 0x2000; addr += 0x10) {
		swift_inv_insn_tag(addr<<1);
		swift_inv_data_tag(addr);
	}
}

/* Swift is so broken, it isn't even safe to use the following. */
static inline void swift_flush_page(unsigned long page)
{
	__asm__ __volatile__("sta %%g0, [%0] %1\n\t"
			     : /* no outputs */
			     : "r" (page), "i" (ASI_M_FLUSH_PAGE)
			     : "memory");
}

static inline void swift_flush_segment(unsigned long addr)
{
	__asm__ __volatile__("sta %%g0, [%0] %1\n\t"
			     : /* no outputs */
			     : "r" (addr), "i" (ASI_M_FLUSH_SEG)
			     : "memory");
}

static inline void swift_flush_region(unsigned long addr)
{
	__asm__ __volatile__("sta %%g0, [%0] %1\n\t"
			     : /* no outputs */
			     : "r" (addr), "i" (ASI_M_FLUSH_REGION)
			     : "memory");
}

static inline void swift_flush_context(void)
{
	__asm__ __volatile__("sta %%g0, [%%g0] %0\n\t"
			     : /* no outputs */
			     : "i" (ASI_M_FLUSH_CTX)
			     : "memory");
}

#endif /* !(_SPARC_SWIFT_H) */
