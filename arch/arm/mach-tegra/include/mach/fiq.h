/*
 * Copyright (C) 2010 Google, Inc.
 *
 * Author:
 *	Iliyan Malchev <malchev@google.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef __ASM_ARCH_TEGRA_FIQ_H
#define __ASM_ARCH_TEGRA_FIQ_H

/* change an interrupt to be an FIQ instead of an IRQ */
void tegra_fiq_select(int n, int on);

/* enable/disable an interrupt that is an FIQ (safe from FIQ context?) */
void tegra_fiq_enable(int n);
void tegra_fiq_disable(int n);

/* install an FIQ handler */
int tegra_fiq_set_handler(void (*func)(void *data, void *regs, void *svc_sp),
		void *data);

#endif
