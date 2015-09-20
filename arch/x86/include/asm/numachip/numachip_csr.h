/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Numascale NumaConnect-Specific Header file
 *
 * Copyright (C) 2011 Numascale AS. All rights reserved.
 *
 * Send feedback to <support@numascale.com>
 *
 */

#ifndef _ASM_X86_NUMACHIP_NUMACHIP_CSR_H
#define _ASM_X86_NUMACHIP_NUMACHIP_CSR_H

#include <linux/io.h>

#define CSR_NODE_SHIFT		16
#define CSR_NODE_BITS(p)	(((unsigned long)(p)) << CSR_NODE_SHIFT)
#define CSR_NODE_MASK		0x0fff		/* 4K nodes */

/* 32K CSR space, b15 indicates geo/non-geo */
#define CSR_OFFSET_MASK	0x7fffUL
#define CSR_G0_NODE_IDS (0x008 + (0 << 12))
#define CSR_G3_EXT_IRQ_GEN (0x030 + (3 << 12))

/*
 * Local CSR space starts in global CSR space with "nodeid" = 0xfff0, however
 * when using the direct mapping on x86_64, both start and size needs to be
 * aligned with PMD_SIZE which is 2M
 */
#define NUMACHIP_LCSR_BASE	0x3ffffe000000ULL
#define NUMACHIP_LCSR_LIM	0x3fffffffffffULL
#define NUMACHIP_LCSR_SIZE	(NUMACHIP_LCSR_LIM - NUMACHIP_LCSR_BASE + 1)

static inline void *lcsr_address(unsigned long offset)
{
	return __va(NUMACHIP_LCSR_BASE | (1UL << 15) |
		CSR_NODE_BITS(0xfff0) | (offset & CSR_OFFSET_MASK));
}

static inline unsigned int read_lcsr(unsigned long offset)
{
	return swab32(readl(lcsr_address(offset)));
}

static inline void write_lcsr(unsigned long offset, unsigned int val)
{
	writel(swab32(val), lcsr_address(offset));
}

#endif /* _ASM_X86_NUMACHIP_NUMACHIP_CSR_H */
