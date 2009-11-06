/*
 * Copyright 2001 MontaVista Software Inc.
 * Author: Jun Sun, jsun@mvista.com or jsun@junsun.net
 * Copyright (C) 2000, 2001 Ralf Baechle (ralf@gnu.org)
 *
 * Copyright (C) 2007 Lemote Inc. & Insititute of Computing Technology
 * Author: Fuxin Zhang, zhangfx@lemote.com
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 */
#include <linux/interrupt.h>
#include <linux/compiler.h>

#include <loongson.h>

static inline void bonito_irq_enable(unsigned int irq)
{
	LOONGSON_INTENSET = (1 << (irq - LOONGSON_IRQ_BASE));
	mmiowb();
}

static inline void bonito_irq_disable(unsigned int irq)
{
	LOONGSON_INTENCLR = (1 << (irq - LOONGSON_IRQ_BASE));
	mmiowb();
}

static struct irq_chip bonito_irq_type = {
	.name	= "bonito_irq",
	.ack	= bonito_irq_disable,
	.mask	= bonito_irq_disable,
	.mask_ack = bonito_irq_disable,
	.unmask	= bonito_irq_enable,
};

static struct irqaction __maybe_unused dma_timeout_irqaction = {
	.handler	= no_action,
	.name		= "dma_timeout",
};

void bonito_irq_init(void)
{
	u32 i;

	for (i = LOONGSON_IRQ_BASE; i < LOONGSON_IRQ_BASE + 32; i++)
		set_irq_chip_and_handler(i, &bonito_irq_type, handle_level_irq);

#ifdef CONFIG_CPU_LOONGSON2E
	setup_irq(LOONGSON_IRQ_BASE + 10, &dma_timeout_irqaction);
#endif
}
