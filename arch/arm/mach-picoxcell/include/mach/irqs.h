/*
 * Copyright (c) 2011 Picochip Ltd., Jamie Iles
 *
 * This file contains the hardware definitions of the picoXcell SoC devices.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#ifndef __MACH_IRQS_H
#define __MACH_IRQS_H

#define ARCH_NR_IRQS			64
#define NR_IRQS				(128 + ARCH_NR_IRQS)

#define IRQ_VIC0_BASE			0
#define IRQ_VIC1_BASE			32

#endif /* __MACH_IRQS_H */
