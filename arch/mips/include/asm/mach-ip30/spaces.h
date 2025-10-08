/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2016 Joshua Kinard <linux@kumba.dev>
 *
 */
#ifndef _ASM_MACH_IP30_SPACES_H
#define _ASM_MACH_IP30_SPACES_H

/*
 * Memory in IP30/Octane is offset 512MB in the physical address space.
 */
#define PHYS_OFFSET	_AC(0x20000000, UL)

#ifdef CONFIG_64BIT
#define CAC_BASE	_AC(0xA800000000000000, UL)
#endif

#include <asm/mach-generic/spaces.h>

#endif /* _ASM_MACH_IP30_SPACES_H */
