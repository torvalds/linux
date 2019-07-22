/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2002, 2004, 2007 by Ralf Baechle <ralf@linux-mips.org>
 */
#ifndef __ASM_MIPS_MACH_SIBYTE_WAR_H
#define __ASM_MIPS_MACH_SIBYTE_WAR_H

#define R4600_V1_INDEX_ICACHEOP_WAR	0
#define R4600_V1_HIT_CACHEOP_WAR	0
#define R4600_V2_HIT_CACHEOP_WAR	0

#if defined(CONFIG_SB1_PASS_2_WORKAROUNDS)

#ifndef __ASSEMBLY__
extern int sb1250_m3_workaround_needed(void);
#endif

#define BCM1250_M3_WAR	sb1250_m3_workaround_needed()
#define SIBYTE_1956_WAR 1

#else

#define BCM1250_M3_WAR	0
#define SIBYTE_1956_WAR 0

#endif

#define MIPS4K_ICACHE_REFILL_WAR	0
#define MIPS_CACHE_SYNC_WAR		0
#define TX49XX_ICACHE_INDEX_INV_WAR	0
#define ICACHE_REFILLS_WORKAROUND_WAR	0
#define R10000_LLSC_WAR			0
#define MIPS34K_MISSED_ITLB_WAR		0

#endif /* __ASM_MIPS_MACH_SIBYTE_WAR_H */
