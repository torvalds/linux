/* irq-routing.c: IRQ routing
 *
 * Copyright (C) 2004 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/sched.h>
#include <linux/random.h>
#include <linux/init.h>
#include <linux/serial_reg.h>
#include <asm/io.h>
#include <asm/irq-routing.h>
#include <asm/irc-regs.h>
#include <asm/serial-regs.h>
#include <asm/dma.h>

struct irq_level frv_irq_levels[16] = {
	[0 ... 15] = {
		.lock	= SPIN_LOCK_UNLOCKED,
	}
};

struct irq_group *irq_groups[NR_IRQ_GROUPS];

extern struct irq_group frv_cpu_irqs;

void __init frv_irq_route(struct irq_source *source, int irqlevel)
{
	source->level = &frv_irq_levels[irqlevel];
	source->next = frv_irq_levels[irqlevel].sources;
	frv_irq_levels[irqlevel].sources = source;
}

void __init frv_irq_route_external(struct irq_source *source, int irq)
{
	int irqlevel = 0;

	switch (irq) {
	case IRQ_CPU_EXTERNAL0:	irqlevel = IRQ_XIRQ0_LEVEL; break;
	case IRQ_CPU_EXTERNAL1:	irqlevel = IRQ_XIRQ1_LEVEL; break;
	case IRQ_CPU_EXTERNAL2:	irqlevel = IRQ_XIRQ2_LEVEL; break;
	case IRQ_CPU_EXTERNAL3:	irqlevel = IRQ_XIRQ3_LEVEL; break;
	case IRQ_CPU_EXTERNAL4:	irqlevel = IRQ_XIRQ4_LEVEL; break;
	case IRQ_CPU_EXTERNAL5:	irqlevel = IRQ_XIRQ5_LEVEL; break;
	case IRQ_CPU_EXTERNAL6:	irqlevel = IRQ_XIRQ6_LEVEL; break;
	case IRQ_CPU_EXTERNAL7:	irqlevel = IRQ_XIRQ7_LEVEL; break;
	default: BUG();
	}

	source->level = &frv_irq_levels[irqlevel];
	source->next = frv_irq_levels[irqlevel].sources;
	frv_irq_levels[irqlevel].sources = source;
}

void __init frv_irq_set_group(struct irq_group *group)
{
	irq_groups[group->first_irq >> NR_IRQ_LOG2_ACTIONS_PER_GROUP] = group;
}

void distribute_irqs(struct irq_group *group, unsigned long irqmask)
{
	struct irqaction *action;
	int irq;

	while (irqmask) {
		asm("scan %1,gr0,%0" : "=r"(irq) : "r"(irqmask));
		if (irq < 0 || irq > 31)
			asm volatile("break");
		irq = 31 - irq;

		irqmask &= ~(1 << irq);
		action = group->actions[irq];

		irq += group->first_irq;

		if (action) {
			int status = 0;

//			if (!(action->flags & SA_INTERRUPT))
//				local_irq_enable();

			do {
				status |= action->flags;
				action->handler(irq, action->dev_id, __frame);
				action = action->next;
			} while (action);

			if (status & SA_SAMPLE_RANDOM)
				add_interrupt_randomness(irq);
			local_irq_disable();
		}
	}
}

/*****************************************************************************/
/*
 * CPU UART interrupts
 */
static void frv_cpuuart_doirq(struct irq_source *source)
{
//	uint8_t iir = readb(source->muxdata + UART_IIR * 8);
//	if ((iir & 0x0f) != UART_IIR_NO_INT)
		distribute_irqs(&frv_cpu_irqs, source->irqmask);
}

struct irq_source frv_cpuuart[2] = {
#define __CPUUART(X, A)						\
	[X] = {							\
		.muxname	= "uart",			\
		.muxdata	= (volatile void __iomem *) A,	\
		.irqmask	= 1 << IRQ_CPU_UART##X,		\
		.doirq		= frv_cpuuart_doirq,		\
	}

	__CPUUART(0, UART0_BASE),
	__CPUUART(1, UART1_BASE),
};

/*****************************************************************************/
/*
 * CPU DMA interrupts
 */
static void frv_cpudma_doirq(struct irq_source *source)
{
	uint32_t cstr = readl(source->muxdata + DMAC_CSTRx);
	if (cstr & DMAC_CSTRx_INT)
		distribute_irqs(&frv_cpu_irqs, source->irqmask);
}

struct irq_source frv_cpudma[8] = {
#define __CPUDMA(X, A)						\
	[X] = {							\
		.muxname	= "dma",			\
		.muxdata	= (volatile void __iomem *) A,	\
		.irqmask	= 1 << IRQ_CPU_DMA##X,		\
		.doirq		= frv_cpudma_doirq,		\
	}

	__CPUDMA(0, 0xfe000900),
	__CPUDMA(1, 0xfe000980),
	__CPUDMA(2, 0xfe000a00),
	__CPUDMA(3, 0xfe000a80),
	__CPUDMA(4, 0xfe001000),
	__CPUDMA(5, 0xfe001080),
	__CPUDMA(6, 0xfe001100),
	__CPUDMA(7, 0xfe001180),
};

/*****************************************************************************/
/*
 * CPU timer interrupts - can't tell whether they've generated an interrupt or not
 */
static void frv_cputimer_doirq(struct irq_source *source)
{
	distribute_irqs(&frv_cpu_irqs, source->irqmask);
}

struct irq_source frv_cputimer[3] = {
#define __CPUTIMER(X)						\
	[X] = {							\
		.muxname	= "timer",			\
		.muxdata	= 0,				\
		.irqmask	= 1 << IRQ_CPU_TIMER##X,	\
		.doirq		= frv_cputimer_doirq,		\
	}

	__CPUTIMER(0),
	__CPUTIMER(1),
	__CPUTIMER(2),
};

/*****************************************************************************/
/*
 * external CPU interrupts - can't tell directly whether they've generated an interrupt or not
 */
static void frv_cpuexternal_doirq(struct irq_source *source)
{
	distribute_irqs(&frv_cpu_irqs, source->irqmask);
}

struct irq_source frv_cpuexternal[8] = {
#define __CPUEXTERNAL(X)					\
	[X] = {							\
		.muxname	= "ext",			\
		.muxdata	= 0,				\
		.irqmask	= 1 << IRQ_CPU_EXTERNAL##X,	\
		.doirq		= frv_cpuexternal_doirq,	\
	}

	__CPUEXTERNAL(0),
	__CPUEXTERNAL(1),
	__CPUEXTERNAL(2),
	__CPUEXTERNAL(3),
	__CPUEXTERNAL(4),
	__CPUEXTERNAL(5),
	__CPUEXTERNAL(6),
	__CPUEXTERNAL(7),
};

#define set_IRR(N,A,B,C,D) __set_IRR(N, (A << 28) | (B << 24) | (C << 20) | (D << 16))

struct irq_group frv_cpu_irqs = {
	.sources = {
		[IRQ_CPU_UART0]		= &frv_cpuuart[0],
		[IRQ_CPU_UART1]		= &frv_cpuuart[1],
		[IRQ_CPU_TIMER0]	= &frv_cputimer[0],
		[IRQ_CPU_TIMER1]	= &frv_cputimer[1],
		[IRQ_CPU_TIMER2]	= &frv_cputimer[2],
		[IRQ_CPU_DMA0]		= &frv_cpudma[0],
		[IRQ_CPU_DMA1]		= &frv_cpudma[1],
		[IRQ_CPU_DMA2]		= &frv_cpudma[2],
		[IRQ_CPU_DMA3]		= &frv_cpudma[3],
		[IRQ_CPU_DMA4]		= &frv_cpudma[4],
		[IRQ_CPU_DMA5]		= &frv_cpudma[5],
		[IRQ_CPU_DMA6]		= &frv_cpudma[6],
		[IRQ_CPU_DMA7]		= &frv_cpudma[7],
		[IRQ_CPU_EXTERNAL0]	= &frv_cpuexternal[0],
		[IRQ_CPU_EXTERNAL1]	= &frv_cpuexternal[1],
		[IRQ_CPU_EXTERNAL2]	= &frv_cpuexternal[2],
		[IRQ_CPU_EXTERNAL3]	= &frv_cpuexternal[3],
		[IRQ_CPU_EXTERNAL4]	= &frv_cpuexternal[4],
		[IRQ_CPU_EXTERNAL5]	= &frv_cpuexternal[5],
		[IRQ_CPU_EXTERNAL6]	= &frv_cpuexternal[6],
		[IRQ_CPU_EXTERNAL7]	= &frv_cpuexternal[7],
	},
};

/*****************************************************************************/
/*
 * route the CPU's interrupt sources
 */
void __init route_cpu_irqs(void)
{
	frv_irq_set_group(&frv_cpu_irqs);

	__set_IITMR(0, 0x003f0000);	/* DMA0-3, TIMER0-2 IRQ detect levels */
	__set_IITMR(1, 0x20000000);	/* ERR0-1, UART0-1, DMA4-7 IRQ detect levels */

	/* route UART and error interrupts */
	frv_irq_route(&frv_cpuuart[0],	IRQ_UART0_LEVEL);
	frv_irq_route(&frv_cpuuart[1],	IRQ_UART1_LEVEL);

	set_IRR(6, IRQ_GDBSTUB_LEVEL, IRQ_GDBSTUB_LEVEL, IRQ_UART1_LEVEL, IRQ_UART0_LEVEL);

	/* route DMA channel interrupts */
	frv_irq_route(&frv_cpudma[0],	IRQ_DMA0_LEVEL);
	frv_irq_route(&frv_cpudma[1],	IRQ_DMA1_LEVEL);
	frv_irq_route(&frv_cpudma[2],	IRQ_DMA2_LEVEL);
	frv_irq_route(&frv_cpudma[3],	IRQ_DMA3_LEVEL);
	frv_irq_route(&frv_cpudma[4],	IRQ_DMA4_LEVEL);
	frv_irq_route(&frv_cpudma[5],	IRQ_DMA5_LEVEL);
	frv_irq_route(&frv_cpudma[6],	IRQ_DMA6_LEVEL);
	frv_irq_route(&frv_cpudma[7],	IRQ_DMA7_LEVEL);

	set_IRR(4, IRQ_DMA3_LEVEL, IRQ_DMA2_LEVEL, IRQ_DMA1_LEVEL, IRQ_DMA0_LEVEL);
	set_IRR(7, IRQ_DMA7_LEVEL, IRQ_DMA6_LEVEL, IRQ_DMA5_LEVEL, IRQ_DMA4_LEVEL);

	/* route timer interrupts */
	frv_irq_route(&frv_cputimer[0],	IRQ_TIMER0_LEVEL);
	frv_irq_route(&frv_cputimer[1],	IRQ_TIMER1_LEVEL);
	frv_irq_route(&frv_cputimer[2],	IRQ_TIMER2_LEVEL);

	set_IRR(5, 0, IRQ_TIMER2_LEVEL, IRQ_TIMER1_LEVEL, IRQ_TIMER0_LEVEL);

	/* route external interrupts */
	frv_irq_route(&frv_cpuexternal[0], IRQ_XIRQ0_LEVEL);
	frv_irq_route(&frv_cpuexternal[1], IRQ_XIRQ1_LEVEL);
	frv_irq_route(&frv_cpuexternal[2], IRQ_XIRQ2_LEVEL);
	frv_irq_route(&frv_cpuexternal[3], IRQ_XIRQ3_LEVEL);
	frv_irq_route(&frv_cpuexternal[4], IRQ_XIRQ4_LEVEL);
	frv_irq_route(&frv_cpuexternal[5], IRQ_XIRQ5_LEVEL);
	frv_irq_route(&frv_cpuexternal[6], IRQ_XIRQ6_LEVEL);
	frv_irq_route(&frv_cpuexternal[7], IRQ_XIRQ7_LEVEL);

	set_IRR(2, IRQ_XIRQ7_LEVEL, IRQ_XIRQ6_LEVEL, IRQ_XIRQ5_LEVEL, IRQ_XIRQ4_LEVEL);
	set_IRR(3, IRQ_XIRQ3_LEVEL, IRQ_XIRQ2_LEVEL, IRQ_XIRQ1_LEVEL, IRQ_XIRQ0_LEVEL);

#if defined(CONFIG_MB93091_VDK)
	__set_TM1(0x55550000);		/* XIRQ7-0 all active low */
#elif defined(CONFIG_MB93093_PDK)
	__set_TM1(0x15550000);		/* XIRQ7 active high, 6-0 all active low */
#else
#error dont know external IRQ trigger levels for this setup
#endif

} /* end route_cpu_irqs() */
