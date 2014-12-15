/*
 * arch/arm/plat-meson/include/plat/irq.h
 *
 * Copyright (C) 2013 Amlogic, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef __PLAT_MESON_IRQ_H
#define __PLAT_MESON_IRQ_H

#include <asm/hardware/gic.h>
void meson_init_gic_irq(void);

#endif
