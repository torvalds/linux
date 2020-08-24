/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2002, 2004, 2007 by Ralf Baechle <ralf@linux-mips.org>
 */
#ifndef __ASM_MIPS_MACH_SIBYTE_WAR_H
#define __ASM_MIPS_MACH_SIBYTE_WAR_H

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

#define MIPS34K_MISSED_ITLB_WAR		0

#endif /* __ASM_MIPS_MACH_SIBYTE_WAR_H */
