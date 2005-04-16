/*
 * c 2001 PPC 64 Team, IBM Corp
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */
#ifndef _PPC_KERNEL_i8259_H
#define _PPC_KERNEL_i8259_H

extern struct hw_interrupt_type i8259_pic;

extern void i8259_init(int offset);
extern int i8259_irq(int);

#endif /* _PPC_KERNEL_i8259_H */
