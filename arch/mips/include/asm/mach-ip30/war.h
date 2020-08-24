/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2002, 2004, 2007 by Ralf Baechle <ralf@linux-mips.org>
 */
#ifndef __ASM_MIPS_MACH_IP30_WAR_H
#define __ASM_MIPS_MACH_IP30_WAR_H

#define BCM1250_M3_WAR			0
#define SIBYTE_1956_WAR			0
#ifdef CONFIG_CPU_R10000
#define R10000_LLSC_WAR			1
#else
#define R10000_LLSC_WAR			0
#endif
#define MIPS34K_MISSED_ITLB_WAR		0

#endif /* __ASM_MIPS_MACH_IP30_WAR_H */
