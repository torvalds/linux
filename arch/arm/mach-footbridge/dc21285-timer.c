/*
 *  linux/arch/arm/mach-footbridge/dc21285-timer.c
 *
 *  Copyright (C) 1998 Russell King.
 *  Copyright (C) 1998 Phil Blundell
 */
#include <linux/init.h>
#include <linux/interrupt.h>

#include <asm/irq.h>

#include <asm/hardware/dec21285.h>
#include <asm/mach/time.h>

#include "common.h"

/*
 * Footbridge timer 1 support.
 */
static unsigned long timer1_latch;

static unsigned long timer1_gettimeoffset (void)
{
	unsigned long value = timer1_latch - *CSR_TIMER1_VALUE;

	return ((tick_nsec / 1000) * value) / timer1_latch;
}

static irqreturn_t
timer1_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	write_seqlock(&xtime_lock);

	*CSR_TIMER1_CLR = 0;

	timer_tick(regs);

	write_sequnlock(&xtime_lock);

	return IRQ_HANDLED;
}

static struct irqaction footbridge_timer_irq = {
	.name		= "Timer1 timer tick",
	.handler	= timer1_interrupt,
	.flags		= SA_INTERRUPT,
};

/*
 * Set up timer interrupt.
 */
static void __init footbridge_timer_init(void)
{
	isa_rtc_init();

	timer1_latch = (mem_fclk_21285 + 8 * HZ) / (16 * HZ);

	*CSR_TIMER1_CLR  = 0;
	*CSR_TIMER1_LOAD = timer1_latch;
	*CSR_TIMER1_CNTL = TIMER_CNTL_ENABLE | TIMER_CNTL_AUTORELOAD | TIMER_CNTL_DIV16;

	setup_irq(IRQ_TIMER1, &footbridge_timer_irq);
}

struct sys_timer footbridge_timer = {
	.init		= footbridge_timer_init,
	.offset		= timer1_gettimeoffset,
};
