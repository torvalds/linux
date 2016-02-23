/*
 * arch/arm/include/asm/hardware/cache-feroceon-l2.h
 *
 * Copyright (C) 2008 Marvell Semiconductor
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

extern void __init feroceon_l2_init(int l2_wt_override);
extern int __init feroceon_of_init(void);

