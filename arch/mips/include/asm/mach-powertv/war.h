/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * This version for the PowerTV platform copied from the Malta version.
 *
 * Copyright (C) 2002, 2004, 2007 by Ralf Baechle <ralf@linux-mips.org>
 * Portions copyright (C) 2009 Cisco Systems, Inc.
 */
#ifndef __ASM_MACH_POWERTV_WAR_H
#define __ASM_MACH_POWERTV_WAR_H

#define R4600_V1_INDEX_ICACHEOP_WAR	0
#define R4600_V1_HIT_CACHEOP_WAR	0
#define R4600_V2_HIT_CACHEOP_WAR	0
#define R5432_CP0_INTERRUPT_WAR		0
#define BCM1250_M3_WAR			0
#define SIBYTE_1956_WAR			0
#define MIPS4K_ICACHE_REFILL_WAR	1
#define MIPS_CACHE_SYNC_WAR		1
#define TX49XX_ICACHE_INDEX_INV_WAR	0
#define ICACHE_REFILLS_WORKAROUND_WAR	1
#define R10000_LLSC_WAR			0
#define MIPS34K_MISSED_ITLB_WAR		0

#endif /* __ASM_MACH_POWERTV_WAR_H */
