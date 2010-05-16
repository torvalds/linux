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

/* This type is the placeholder for a hardware interrupt number. It has to
 * be big enough to enclose whatever representation is used by a given
 * platform.
 */
typedef unsigned long irq_hw_number_t;

extern unsigned int nr_irq;

#define NO_IRQ (-1)

struct pt_regs;
extern void do_IRQ(struct pt_regs *regs);

/**
 * irq_of_parse_and_map - Parse and Map an interrupt into linux virq space
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

struct irq_host;

/**
 * irq_create_mapping - Map a hardware interrupt into linux virq space
 * @host: host owning this hardware interrupt or NULL for default host
 * @hwirq: hardware irq number in that host space
 *
 * Only one mapping per hardware interrupt is permitted. Returns a linux
 * virq number.
 * If the sense/trigger is to be specified, set_irq_type() should be called
 * on the number returned from that call.
 */
extern unsigned int irq_create_mapping(struct irq_host *host,
					irq_hw_number_t hwirq);

/**
 * irq_create_of_mapping - Map a hardware interrupt into linux virq space
 * @controller: Device node of the interrupt controller
 * @inspec: Interrupt specifier from the device-tree
 * @intsize: Size of the interrupt specifier from the device-tree
 *
 * This function is identical to irq_create_mapping except that it takes
 * as input informations straight from the device-tree (typically the results
 * of the of_irq_map_*() functions.
 */
extern unsigned int irq_create_of_mapping(struct device_node *controller,
					u32 *intspec, unsigned int intsize);

#endif /* _ASM_MICROBLAZE_IRQ_H */
