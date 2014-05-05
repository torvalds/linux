/*
 * Board functions for Marvell System On Chip
 *
 * Copyright (C) 2014
 *
 * Andrew Lunn <andrew@lunn.ch>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#ifndef __ARCH_MVEBU_BOARD_H
#define __ARCH_MVEBU_BOARD_H

#ifdef CONFIG_MACH_T5325
void t5325_init(void);
#else
static inline void t5325_init(void) {};
#endif

#endif
