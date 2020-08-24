/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2002, 2004, 2007 by Ralf Baechle <ralf@linux-mips.org>
 * Copyright (C) 2008 Cavium Networks <support@caviumnetworks.com>
 */
#ifndef __ASM_MIPS_MACH_CAVIUM_OCTEON_WAR_H
#define __ASM_MIPS_MACH_CAVIUM_OCTEON_WAR_H

#define BCM1250_M3_WAR			0

#define CAVIUM_OCTEON_DCACHE_PREFETCH_WAR	\
	OCTEON_IS_MODEL(OCTEON_CN6XXX)

#endif /* __ASM_MIPS_MACH_CAVIUM_OCTEON_WAR_H */
