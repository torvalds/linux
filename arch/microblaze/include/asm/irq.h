/*
 * Copyright (C) 2006 Atmark Techno, Inc.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#ifndef _ASM_MICROBLAZE_IRQ_H
#define _ASM_MICROBLAZE_IRQ_H

#define NR_IRQS 32
#include <asm-generic/irq.h>

#include <linux/interrupt.h>

extern unsigned int nr_irq;

#define NO_IRQ (-1)

struct pt_regs;
extern void do_IRQ(struct pt_regs *regs);

/* irq_of_parse_and_map - Parse and Map an interrupt into linux virq space
 * @device: Device node of the device whose interrupt is to be mapped
 * @index: Index of the interrupt to map
 *
 * This function is a wrapper that chains of_irq_map_one() and
 * irq_create_of_mapping() to make things easier to callers
 */
struct device_node;
extern unsigned int irq_of_parse_and_map(struct device_node *dev, int index);

/** FIXME - not implement
 * irq_dispose_mapping - Unmap an interrupt
 * @virq: linux virq number of the interrupt to unmap
 */
static inline void irq_dispose_mapping(unsigned int virq)
{
	return;
}

#endif /* _ASM_MICROBLAZE_IRQ_H */
