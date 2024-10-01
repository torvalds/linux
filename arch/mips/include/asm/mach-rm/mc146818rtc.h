/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2004 by Ralf Baechle
 *
 * RTC routines for PC style attached Dallas chip with ARC epoch.
 */
#ifndef __ASM_MACH_RM_MC146818RTC_H
#define __ASM_MACH_RM_MC146818RTC_H

#ifdef CONFIG_CPU_BIG_ENDIAN
#define mc146818_decode_year(year) ((year) < 70 ? (year) + 2000 : (year) + 1900)
#else
#define mc146818_decode_year(year) ((year) + 1980)
#endif

#include <asm/mach-generic/mc146818rtc.h>

#endif /* __ASM_MACH_RM_MC146818RTC_H */
