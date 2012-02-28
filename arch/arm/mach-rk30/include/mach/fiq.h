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

#ifndef __ASM_ARCH_RK30_FIQ_H
#define __ASM_ARCH_RK30_FIQ_H

/* enable/disable an interrupt that is an FIQ (safe from FIQ context?) */
void rk_fiq_enable(int n);
void rk_fiq_disable(int n);
void rk_irq_setpending(int irq);
void rk_irq_clearpending(int irq);
void rk30_fiq_init(void);
#endif
