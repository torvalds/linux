/*
 * System timer for Freescale STMP37XX/STMP378X
 *
 * Embedded Alley Solutions, Inc <source@embeddedalley.com>
 *
 * Copyright 2008 Freescale Semiconductor, Inc. All Rights Reserved.
 * Copyright 2008 Embedded Alley Solutions, Inc All Rights Reserved.
 */

/*
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/clocksource.h>
#include <linux/clockchips.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/interrupt.h>

#include <asm/mach/time.h>
#include <mach/stmp3xxx.h>
#include <mach/regs-timrot.h>

static irqreturn_t
stmp3xxx_timer_interrupt(int irq, void *dev_id)
{
	struct clock_event_device *c = dev_id;

	if (HW_TIMROT_TIMCTRLn_RD(0) & (1<<15)) {
		HW_TIMROT_TIMCTRLn_CLR(0, (1<<15));
		c->event_handler(c);
	} else if (HW_TIMROT_TIMCTRLn_RD(1) & (1<<15)) {
		HW_TIMROT_TIMCTRLn_CLR(1, (1<<15));
		HW_TIMROT_TIMCTRLn_CLR(1, BM_TIMROT_TIMCTRLn_IRQ_EN);
		HW_TIMROT_TIMCOUNTn_WR(1, 0xFFFF);
	}

	return IRQ_HANDLED;
}

static cycle_t stmp3xxx_clock_read(void)
{
	return ~((HW_TIMROT_TIMCOUNTn_RD(1) & 0xFFFF0000) >> 16);
}

static int
stmp3xxx_timrot_set_next_event(unsigned long delta,
		struct clock_event_device *dev)
{
	HW_TIMROT_TIMCOUNTn_WR(0, delta); /* reload */
	return 0;
}

static void
stmp3xxx_timrot_set_mode(enum clock_event_mode mode,
		struct clock_event_device *dev)
{
}

static struct clock_event_device ckevt_timrot = {
	.name		= "timrot",
	.features	= CLOCK_EVT_FEAT_ONESHOT,
	.shift		= 32,
	.set_next_event	= stmp3xxx_timrot_set_next_event,
	.set_mode	= stmp3xxx_timrot_set_mode,
};

static struct clocksource cksrc_stmp3xxx = {
	.name           = "cksrc_stmp3xxx",
	.rating         = 250,
	.read           = stmp3xxx_clock_read,
	.mask           = CLOCKSOURCE_MASK(16),
	.shift          = 10,
	.flags		= CLOCK_SOURCE_IS_CONTINUOUS,
};

static struct irqaction stmp3xxx_timer_irq = {
	.name		= "stmp3xxx_timer",
	.flags		= IRQF_DISABLED | IRQF_TIMER,
	.handler	= stmp3xxx_timer_interrupt,
	.dev_id		= &ckevt_timrot,
};


/*
 * Set up timer interrupt, and return the current time in seconds.
 */
static void __init stmp3xxx_init_timer(void)
{
	cksrc_stmp3xxx.mult = clocksource_hz2mult(CLOCK_TICK_RATE,
				cksrc_stmp3xxx.shift);
	ckevt_timrot.mult = div_sc(CLOCK_TICK_RATE, NSEC_PER_SEC,
				ckevt_timrot.shift);
	ckevt_timrot.min_delta_ns = clockevent_delta2ns(2, &ckevt_timrot);
	ckevt_timrot.max_delta_ns = clockevent_delta2ns(0xFFF, &ckevt_timrot);
	ckevt_timrot.cpumask = cpumask_of(0);

	HW_TIMROT_ROTCTRL_CLR(BM_TIMROT_ROTCTRL_SFTRST |
				BM_TIMROT_ROTCTRL_CLKGATE);
	HW_TIMROT_TIMCOUNTn_WR(0, 0);
	HW_TIMROT_TIMCOUNTn_WR(1, 0);

	HW_TIMROT_TIMCTRLn_WR(0,
			      (BF_TIMROT_TIMCTRLn_SELECT(8) |  /* 32 kHz */
			       BF_TIMROT_TIMCTRLn_PRESCALE(0) |
			       BM_TIMROT_TIMCTRLn_RELOAD |
			       BM_TIMROT_TIMCTRLn_UPDATE |
			       BM_TIMROT_TIMCTRLn_IRQ_EN));
	HW_TIMROT_TIMCTRLn_WR(1,
			      (BF_TIMROT_TIMCTRLn_SELECT(8) |  /* 32 kHz */
			       BF_TIMROT_TIMCTRLn_PRESCALE(0) |
			       BM_TIMROT_TIMCTRLn_RELOAD |
			       BM_TIMROT_TIMCTRLn_UPDATE));

	HW_TIMROT_TIMCOUNTn_WR(0, CLOCK_TICK_RATE / HZ - 1);
	HW_TIMROT_TIMCOUNTn_WR(1, 0xFFFF); /* reload */

	setup_irq(IRQ_TIMER0, &stmp3xxx_timer_irq);

	clocksource_register(&cksrc_stmp3xxx);
	clockevents_register_device(&ckevt_timrot);
}

#ifdef CONFIG_PM

void stmp3xxx_suspend_timer(void)
{
	HW_TIMROT_TIMCTRLn_CLR(0, BM_TIMROT_TIMCTRLn_IRQ_EN);
	HW_TIMROT_TIMCTRLn_CLR(0, (1<<15));
	HW_TIMROT_ROTCTRL_SET(BM_TIMROT_ROTCTRL_CLKGATE);
}

void stmp3xxx_resume_timer(void)
{
	HW_TIMROT_ROTCTRL_CLR(BM_TIMROT_ROTCTRL_SFTRST |
				BM_TIMROT_ROTCTRL_CLKGATE);


	HW_TIMROT_TIMCTRLn_WR(0,
			      (BF_TIMROT_TIMCTRLn_SELECT(8) |  /* 32 kHz */
			       BF_TIMROT_TIMCTRLn_PRESCALE(0) |
			       BM_TIMROT_TIMCTRLn_UPDATE |
			       BM_TIMROT_TIMCTRLn_IRQ_EN));
	HW_TIMROT_TIMCTRLn_WR(1,
			      (BF_TIMROT_TIMCTRLn_SELECT(8) |  /* 32 kHz */
			       BF_TIMROT_TIMCTRLn_PRESCALE(0) |
			       BM_TIMROT_TIMCTRLn_RELOAD |
			       BM_TIMROT_TIMCTRLn_UPDATE));

	HW_TIMROT_TIMCOUNTn_WR(0, CLOCK_TICK_RATE / HZ - 1);
	HW_TIMROT_TIMCOUNTn_WR(1, 0xFFFF); /* reload */
}

#else

#define stmp3xxx_suspend_timer	NULL
#define	stmp3xxx_resume_timer	NULL

#endif	/* CONFIG_PM */

struct sys_timer stmp3xxx_timer = {
	.init		= stmp3xxx_init_timer,
	.suspend	= stmp3xxx_suspend_timer,
	.resume		= stmp3xxx_resume_timer,
};
