/*
 * Marvell MVEBU CPU clock handling.
 *
 * Copyright (C) 2012 Marvell
 *
 * Gregory CLEMENT <gregory.clement@free-electrons.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#ifndef __MVEBU_CLK_CPU_H
#define __MVEBU_CLK_CPU_H

#ifdef CONFIG_MVEBU_CLK_CPU
void __init mvebu_cpu_clk_init(void);
#else
static inline void mvebu_cpu_clk_init(void) {}
#endif

#endif
