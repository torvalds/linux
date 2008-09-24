#ifndef __ASM_SH_CPU_SH5_CACHE_H
#define __ASM_SH_CPU_SH5_CACHE_H

/*
 * include/asm-sh/cpu-sh5/cache.h
 *
 * Copyright (C) 2000, 2001  Paolo Alberelli
 * Copyright (C) 2003, 2004  Paul Mundt
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#define L1_CACHE_SHIFT		5

/* Valid and Dirty bits */
#define SH_CACHE_VALID		(1LL<<0)
#define SH_CACHE_UPDATED	(1LL<<57)

/* Unimplemented compat bits.. */
#define SH_CACHE_COMBINED	0
#define SH_CACHE_ASSOC		0

/* Cache flags */
#define SH_CACHE_MODE_WT	(1LL<<0)
#define SH_CACHE_MODE_WB	(1LL<<1)

/*
 * Control Registers.
 */
#define ICCR_BASE	0x01600000	/* Instruction Cache Control Register */
#define ICCR_REG0	0		/* Register 0 offset */
#define ICCR_REG1	1		/* Register 1 offset */
#define ICCR0		ICCR_BASE+ICCR_REG0
#define ICCR1		ICCR_BASE+ICCR_REG1

#define ICCR0_OFF	0x0		/* Set ICACHE off */
#define ICCR0_ON	0x1		/* Set ICACHE on */
#define ICCR0_ICI	0x2		/* Invalidate all in IC */

#define ICCR1_NOLOCK	0x0		/* Set No Locking */

#define OCCR_BASE	0x01E00000	/* Operand Cache Control Register */
#define OCCR_REG0	0		/* Register 0 offset */
#define OCCR_REG1	1		/* Register 1 offset */
#define OCCR0		OCCR_BASE+OCCR_REG0
#define OCCR1		OCCR_BASE+OCCR_REG1

#define OCCR0_OFF	0x0		/* Set OCACHE off */
#define OCCR0_ON	0x1		/* Set OCACHE on */
#define OCCR0_OCI	0x2		/* Invalidate all in OC */
#define OCCR0_WT	0x4		/* Set OCACHE in WT Mode */
#define OCCR0_WB	0x0		/* Set OCACHE in WB Mode */

#define OCCR1_NOLOCK	0x0		/* Set No Locking */

/*
 * SH-5
 * A bit of description here, for neff=32.
 *
 *                               |<--- tag  (19 bits) --->|
 * +-----------------------------+-----------------+------+----------+------+
 * |                             |                 | ways |set index |offset|
 * +-----------------------------+-----------------+------+----------+------+
 *                                ^                 2 bits   8 bits   5 bits
 *                                +- Bit 31
 *
 * Cacheline size is based on offset: 5 bits = 32 bytes per line
 * A cache line is identified by a tag + set but OCACHETAG/ICACHETAG
 * have a broader space for registers. These are outlined by
 * CACHE_?C_*_STEP below.
 *
 */

/* Instruction cache */
#define CACHE_IC_ADDRESS_ARRAY 0x01000000

/* Operand Cache */
#define CACHE_OC_ADDRESS_ARRAY 0x01800000

/* These declarations relate to cache 'synonyms' in the operand cache.  A
   'synonym' occurs where effective address bits overlap between those used for
   indexing the cache sets and those passed to the MMU for translation.  In the
   case of SH5-101 & SH5-103, only bit 12 is affected for 4k pages. */

#define CACHE_OC_N_SYNBITS  1               /* Number of synonym bits */
#define CACHE_OC_SYN_SHIFT  12
/* Mask to select synonym bit(s) */
#define CACHE_OC_SYN_MASK   (((1UL<<CACHE_OC_N_SYNBITS)-1)<<CACHE_OC_SYN_SHIFT)

/*
 * Instruction cache can't be invalidated based on physical addresses.
 * No Instruction Cache defines required, then.
 */

#endif /* __ASM_SH_CPU_SH5_CACHE_H */
