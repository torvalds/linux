/*
 * Copyright 2001 MontaVista Software Inc.
 * Author: jsun@mvista.com or jsun@junsun.net
 *
 * rtc and time ops for vr4181.	 Part of code is drived from
 * linux-vr, originally written	 by Bradley D. LaRonde & Michael Klar.
 *
 * This program is free software; you can redistribute	it and/or modify it
 * under  the terms of	the GNU General	 Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 */

#include <linux/kernel.h>
#include <linux/spinlock.h>
#include <linux/param.h>			/* for HZ */
#include <linux/time.h>
#include <linux/interrupt.h>

#include <asm/system.h>
#include <asm/time.h>

#include <asm/vr4181/vr4181.h>

#define COUNTS_PER_JIFFY ((32768 + HZ/2) / HZ)

/*
 * RTC ops
 */

DEFINE_SPINLOCK(rtc_lock);

/* per VR41xx docs, bad data can be read if between 2 counts */
static inline unsigned short
read_time_reg(volatile unsigned short *reg)
{
	unsigned short value;
	do {
		value = *reg;
		barrier();
	} while (value != *reg);
	return value;
}

static unsigned long
vr4181_rtc_get_time(void)
{
	unsigned short regh, regm, regl;

	// why this crazy order, you ask?  to guarantee that neither m
	// nor l wrap before all 3 read
	do {
		regm = read_time_reg(VR4181_ETIMEMREG);
		barrier();
		regh = read_time_reg(VR4181_ETIMEHREG);
		barrier();
		regl = read_time_reg(VR4181_ETIMELREG);
	} while (regm != read_time_reg(VR4181_ETIMEMREG));
	return ((regh << 17) | (regm << 1) | (regl >> 15));
}

static int
vr4181_rtc_set_time(unsigned long timeval)
{
	unsigned short intreg;
	unsigned long flags;

	spin_lock_irqsave(&rtc_lock, flags);
	intreg = *VR4181_RTCINTREG & 0x05;
	barrier();
	*VR4181_ETIMELREG = timeval << 15;
	*VR4181_ETIMEMREG = timeval >> 1;
	*VR4181_ETIMEHREG = timeval >> 17;
	barrier();
	// assume that any ints that just triggered are invalid, since the
	// time value is written non-atomically in 3 separate regs
	*VR4181_RTCINTREG = 0x05 ^ intreg;
	spin_unlock_irqrestore(&rtc_lock, flags);

	return 0;
}


/*
 * timer interrupt routine (wrapper)
 *
 * we need our own interrupt routine because we need to clear
 * RTC1 interrupt.
 */
static void
vr4181_timer_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	/* Clear the interrupt. */
	*VR4181_RTCINTREG = 0x2;

	/* call the generic one */
	timer_interrupt(irq, dev_id, regs);
}


/*
 * vr4181_time_init:
 *
 * We pick the following choices:
 *   . we use elapsed timer as the RTC.	 We set some reasonable init data since
 *     it does not persist across reset
 *   . we use RTC1 as the system timer interrupt source.
 *   . we use CPU counter for fast_gettimeoffset and we calivrate the cpu
 *     frequency.  In other words, we use calibrate_div64_gettimeoffset().
 *   . we use our own timer interrupt routine which clears the interrupt
 *     and then calls the generic high-level timer interrupt routine.
 *
 */

extern int setup_irq(unsigned int irq, struct irqaction *irqaction);

static void
vr4181_timer_setup(struct irqaction *irq)
{
	/* over-write the handler to be our own one */
	irq->handler = vr4181_timer_interrupt;

	/* sets up the frequency */
	*VR4181_RTCL1LREG = COUNTS_PER_JIFFY;
	*VR4181_RTCL1HREG = 0;

	/* and ack any pending ints */
	*VR4181_RTCINTREG = 0x2;

	/* setup irqaction */
	setup_irq(VR4181_IRQ_INT1, irq);

}

void
vr4181_init_time(void)
{
	/* setup hookup functions */
	rtc_get_time = vr4181_rtc_get_time;
	rtc_set_time = vr4181_rtc_set_time;

	board_timer_setup = vr4181_timer_setup;
}

