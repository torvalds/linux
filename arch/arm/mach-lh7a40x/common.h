/* arch/arm/mach-lh7a40x/common.h
 *
 *  Copyright (C) 2004 Marc Singer
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  version 2 as published by the Free Software Foundation.
 *
 */

extern struct sys_timer lh7a40x_timer;

extern void lh7a400_init_irq (void);
extern void lh7a404_init_irq (void);
extern void lh7a40x_init_board_irq (void);

#define IRQ_DISPATCH(irq) desc_handle_irq((irq),(irq_desc + irq), regs)
