/*
 * Copyright (C) 2012 Google, Inc.
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

#ifndef __MACH_EXYNOS_FIQ_DEBUGGER_H
#define __MACH_EXYNOS_FIQ_DEBUGGER_H

#ifdef CONFIG_EXYNOS_FIQ_DEBUGGER
int exynos_serial_debug_init(int id, bool is_fiq);

#else
static inline int exynos_serial_debug_init(int id, bool is_fiq)
{
	return 0;
}
#endif

#endif
