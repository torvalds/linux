/*
 *  linux/arch/arm/mach-aaec2000/core.c
 *
 *  Code common to all AAEC-2000 machines
 *
 *  Copyright (c) 2005 Nicolas Bellido Y Ortega
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */
#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/timex.h>
#include <linux/signal.h>

#include <asm/hardware.h>
#include <asm/irq.h>

#include <asm/mach/irq.h>
#include <asm/mach/time.h>
#include <asm/mach/map.h>

/*
 * Common I/O mapping:
 *
 * Static virtual address mappings are as follow:
 *
 * 0xf8000000-0xf8001ffff: Devices connected to APB bus
 * 0xf8002000-0xf8003ffff: Devices connected to AHB bus
 *
 * Below 0xe8000000 is reserved for vm allocation.
 *
 * The machine specific code must provide the extra mapping beside the
 * default mapping provided here.
 */
static struct map_desc standard_io_desc[] __initdata = {
 /* virtual         physical       length           type */
  { VIO_APB_BASE,   PIO_APB_BASE,  IO_APB_LENGTH,   MT_DEVICE },
  { VIO_AHB_BASE,   PIO_AHB_BASE,  IO_AHB_LENGTH,   MT_DEVICE }
};

void __init aaec2000_map_io(void)
{
	iotable_init(standard_io_desc, ARRAY_SIZE(standard_io_desc));
}

/*
 * Interrupt handling routines
 */
static void aaec2000_int_ack(unsigned int irq)
{
	IRQ_INTSR = 1 << irq;
}

static void aaec2000_int_mask(unsigned int irq)
{
	IRQ_INTENC |= (1 << irq);
}

static void aaec2000_int_unmask(unsigned int irq)
{
	IRQ_INTENS |= (1 << irq);
}

static struct irqchip aaec2000_irq_chip = {
	.ack	= aaec2000_int_ack,
	.mask	= aaec2000_int_mask,
	.unmask	= aaec2000_int_unmask,
};

void __init aaec2000_init_irq(void)
{
	unsigned int i;

	for (i = 0; i < NR_IRQS; i++) {
		set_irq_handler(i, do_level_IRQ);
		set_irq_chip(i, &aaec2000_irq_chip);
		set_irq_flags(i, IRQF_VALID);
	}

	/* Disable all interrupts */
	IRQ_INTENC = 0xffffffff;

	/* Clear any pending interrupts */
	IRQ_INTSR = IRQ_INTSR;
}

/*
 * Time keeping
 */
/* IRQs are disabled before entering here from do_gettimeofday() */
static unsigned long aaec2000_gettimeoffset(void)
{
	unsigned long ticks_to_match, elapsed, usec;

	/* Get ticks before next timer match */
	ticks_to_match = TIMER1_LOAD - TIMER1_VAL;

	/* We need elapsed ticks since last match */
	elapsed = LATCH - ticks_to_match;

	/* Now, convert them to usec */
	usec = (unsigned long)(elapsed * (tick_nsec / 1000))/LATCH;

	return usec;
}

/* We enter here with IRQs enabled */
static irqreturn_t
aaec2000_timer_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	/* TODO: Check timer accuracy */
	write_seqlock(&xtime_lock);

	timer_tick(regs);
	TIMER1_CLEAR = 1;

	write_sequnlock(&xtime_lock);

	return IRQ_HANDLED;
}

static struct irqaction aaec2000_timer_irq = {
	.name		= "AAEC-2000 Timer Tick",
	.flags		= SA_INTERRUPT | SA_TIMER,
	.handler	= aaec2000_timer_interrupt,
};

static void __init aaec2000_timer_init(void)
{
	/* Disable timer 1 */
	TIMER1_CTRL = 0;

	/* We have somehow to generate a 100Hz clock.
	 * We then use the 508KHz timer in periodic mode.
	 */
	TIMER1_LOAD = LATCH;
	TIMER1_CLEAR = 1; /* Clear interrupt */

	setup_irq(INT_TMR1_OFL, &aaec2000_timer_irq);

	TIMER1_CTRL = TIMER_CTRL_ENABLE |
	                TIMER_CTRL_PERIODIC |
	                TIMER_CTRL_CLKSEL_508K;
}

struct sys_timer aaec2000_timer = {
	.init		= aaec2000_timer_init,
	.offset		= aaec2000_gettimeoffset,
};

