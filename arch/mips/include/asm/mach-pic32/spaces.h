/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Joshua Henderson <joshua.henderson@microchip.com>
 * Copyright (C) 2015 Microchip Technology Inc.  All rights reserved.
 */
#ifndef _ASM_MACH_PIC32_SPACES_H
#define _ASM_MACH_PIC32_SPACES_H

#ifdef CONFIG_PIC32MZDA
#define PHYS_OFFSET	_AC(0x08000000, UL)
#endif

#include <asm/mach-generic/spaces.h>

#endif /* __ASM_MACH_PIC32_SPACES_H */
