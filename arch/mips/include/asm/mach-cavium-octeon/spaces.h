/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2012 Cavium, Inc.
 */
#ifndef _ASM_MACH_CAVIUM_OCTEON_SPACES_H
#define _ASM_MACH_CAVIUM_OCTEON_SPACES_H

#include <linux/const.h>

#ifdef CONFIG_64BIT
/* They are all the same and some OCTEON II cores cannot handle 0xa8.. */
#define CAC_BASE		_AC(0x8000000000000000, UL)
#define UNCAC_BASE		_AC(0x8000000000000000, UL)
#define IO_BASE			_AC(0x8000000000000000, UL)


#endif /* CONFIG_64BIT */

#include <asm/mach-generic/spaces.h>

#endif /* _ASM_MACH_CAVIUM_OCTEON_SPACES_H */
