/*
 * Copyright 2002 Momentum Computer
 * Author: mdharm@momenco.com
 * Copyright (C) 2004, 06 Ralf Baechle <ralf@linux-mips.org>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/kernel_stat.h>
#include <linux/mv643xx.h>
#include <linux/sched.h>

#include <asm/io.h>
#include <asm/irq.h>
#include <asm/marvell.h>

static unsigned int irq_base;

static inline int ls1bit32(unsigned int x)
{
        int b = 31, s;

        s = 16; if (x << 16 == 0) s = 0; b -= s; x <<= s;
        s =  8; if (x <<  8 == 0) s = 0; b -= s; x <<= s;
        s =  4; if (x <<  4 == 0) s = 0; b -= s; x <<= s;
        s =  2; if (x <<  2 == 0) s = 0; b -= s; x <<= s;
        s =  1; if (x <<  1 == 0) s = 0; b -= s;

        return b;
}

/* mask off an interrupt -- 1 is enable, 0 is disable */
static inline void mask_mv64340_irq(unsigned int irq)
{
	uint32_t value;

	if (irq < (irq_base + 32)) {
		value = MV_READ(MV64340_INTERRUPT0_MASK_0_LOW);
		value &= ~(1 << (irq - irq_base));
		MV_WRITE(MV64340_INTERRUPT0_MASK_0_LOW, value);
	} else {
		value = MV_READ(MV64340_INTERRUPT0_MASK_0_HIGH);
		value &= ~(1 << (irq - irq_base - 32));
		MV_WRITE(MV64340_INTERRUPT0_MASK_0_HIGH, value);
	}
}

/* unmask an interrupt -- 1 is enable, 0 is disable */
static inline void unmask_mv64340_irq(unsigned int irq)
{
	uint32_t value;

	if (irq < (irq_base + 32)) {
		value = MV_READ(MV64340_INTERRUPT0_MASK_0_LOW);
		value |= 1 << (irq - irq_base);
		MV_WRITE(MV64340_INTERRUPT0_MASK_0_LOW, value);
	} else {
		value = MV_READ(MV64340_INTERRUPT0_MASK_0_HIGH);
		value |= 1 << (irq - irq_base - 32);
		MV_WRITE(MV64340_INTERRUPT0_MASK_0_HIGH, value);
	}
}

/*
 * Interrupt handler for interrupts coming from the Marvell chip.
 * It could be built in ethernet ports etc...
 */
void ll_mv64340_irq(void)
{
	unsigned int irq_src_low, irq_src_high;
 	unsigned int irq_mask_low, irq_mask_high;

	/* read the interrupt status registers */
	irq_mask_low = MV_READ(MV64340_INTERRUPT0_MASK_0_LOW);
	irq_mask_high = MV_READ(MV64340_INTERRUPT0_MASK_0_HIGH);
	irq_src_low = MV_READ(MV64340_MAIN_INTERRUPT_CAUSE_LOW);
	irq_src_high = MV_READ(MV64340_MAIN_INTERRUPT_CAUSE_HIGH);

	/* mask for just the interrupts we want */
	irq_src_low &= irq_mask_low;
	irq_src_high &= irq_mask_high;

	if (irq_src_low)
		do_IRQ(ls1bit32(irq_src_low) + irq_base);
	else
		do_IRQ(ls1bit32(irq_src_high) + irq_base + 32);
}

struct irq_chip mv64340_irq_type = {
	.typename = "MV-64340",
	.ack = mask_mv64340_irq,
	.mask = mask_mv64340_irq,
	.mask_ack = mask_mv64340_irq,
	.unmask = unmask_mv64340_irq,
};

void __init mv64340_irq_init(unsigned int base)
{
	int i;

	for (i = base; i < base + 64; i++)
		set_irq_chip_and_handler(i, &mv64340_irq_type,
					 handle_level_irq);

	irq_base = base;
}
