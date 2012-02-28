/*
 * linux/arch/arm/mach-rk30/include/mach/rk30_fiq_debugger.h
 *
 * Copyright (C) 2010 Google, Inc.
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

#ifndef __MACH_RK30_FIQ_DEBUGGER_H
#define __MACH_RK30_FIQ_DEBUGGER_H

//#ifdef CONFIG_RK30_FIQ_DEBUGGER
#ifdef CONFIG_FIQ_DEBUGGER
void rk_serial_debug_init(unsigned int base, int irq, int signal, int wakeup_irq);
#else
static inline void rk_serial_debug_init(unsigned int base, int irq, int signal, int wakeup_irq)
{
}
#endif

#endif
