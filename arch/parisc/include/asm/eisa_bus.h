/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * eisa_bus.h interface between the eisa BA driver and the bus enumerator
 *
 * Copyright (c) 2002 Daniel Engstrom <5116@telia.com>
 */

#ifndef ASM_EISA_H
#define ASM_EISA_H

extern void eisa_make_irq_level(int num);
extern void eisa_make_irq_edge(int num);
extern int eisa_enumerator(unsigned long eeprom_addr,
			   struct resource *io_parent, 
			   struct resource *mem_parent);
extern int eisa_eeprom_init(unsigned long addr);

#endif
