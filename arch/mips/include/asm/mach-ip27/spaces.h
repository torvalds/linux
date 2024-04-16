/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1996, 99 Ralf Baechle
 * Copyright (C) 2000, 2002  Maciej W. Rozycki
 * Copyright (C) 1990, 1999 by Silicon Graphics, Inc.
 */
#ifndef _ASM_MACH_IP27_SPACES_H
#define _ASM_MACH_IP27_SPACES_H

#include <linux/const.h>

/*
 * IP27 uses the R10000's uncached attribute feature.  Attribute 3 selects
 * uncached memory addressing. Hide the definitions on 32-bit compilation
 * of the compat-vdso code.
 */
#ifdef CONFIG_64BIT
#define HSPEC_BASE		_AC(0x9000000000000000, UL)
#define IO_BASE			_AC(0x9200000000000000, UL)
#define MSPEC_BASE		_AC(0x9400000000000000, UL)
#define UNCAC_BASE		_AC(0x9600000000000000, UL)
#define CAC_BASE		_AC(0xa800000000000000, UL)
#endif

#define TO_MSPEC(x)		(MSPEC_BASE | ((x) & TO_PHYS_MASK))
#define TO_HSPEC(x)		(HSPEC_BASE | ((x) & TO_PHYS_MASK))

#define HIGHMEM_START		(~0UL)

#include <asm/mach-generic/spaces.h>

#endif /* _ASM_MACH_IP27_SPACES_H */
