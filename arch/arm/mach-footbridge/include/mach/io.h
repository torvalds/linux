/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *  arch/arm/mach-footbridge/include/mach/io.h
 *
 *  Copyright (C) 1997-1999 Russell King
 *
 *  Modifications:
 *   06-12-1997	RMK	Created.
 *   07-04-1999	RMK	Major cleanup
 */
#ifndef __ASM_ARM_ARCH_IO_H
#define __ASM_ARM_ARCH_IO_H

/*
 * Translation of various i/o addresses to host addresses for !CONFIG_MMU
 */
#define PCIO_BASE       0x7c000000
#define __io(a)			((void __iomem *)(PCIO_BASE + (a)))

#endif
