/*
 * Joshua Henderson <joshua.henderson@microchip.com>
 * Copyright (C) 2015 Microchip Technology Inc.  All rights reserved.
 *
 * This program is free software; you can distribute it and/or modify it
 * under the terms of the GNU General Public License (Version 2) as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 */
#ifndef _ASM_MACH_PIC32_SPACES_H
#define _ASM_MACH_PIC32_SPACES_H

#ifdef CONFIG_PIC32MZDA
#define PHYS_OFFSET	_AC(0x08000000, UL)
#define UNCAC_BASE	_AC(0xa8000000, UL)
#endif

#include <asm/mach-generic/spaces.h>

#endif /* __ASM_MACH_PIC32_SPACES_H */
