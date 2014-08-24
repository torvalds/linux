/*
 * OpenRISC Linux
 *
 * Linux architectural port borrowing liberally from similar works of
 * others.  All original copyrights apply as per the original source
 * declaration.
 *
 * OpenRISC implementation:
 * Copyright (C) 2003 Matjaz Breskvar <phoenix@bsemi.com>
 * Copyright (C) 2010-2011 Jonas Bonn <jonas@southpole.se>
 * et al.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef __ASM_OPENRISC_IRQ_H__
#define __ASM_OPENRISC_IRQ_H__

#define	NR_IRQS		32
#include <asm-generic/irq.h>

#define NO_IRQ		(-1)

void handle_IRQ(unsigned int, struct pt_regs *);
extern void set_handle_irq(void (*handle_irq)(struct pt_regs *));

#endif /* __ASM_OPENRISC_IRQ_H__ */
