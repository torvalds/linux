#include <linux/init.h>
#include <linux/clocksource.h>
#include <linux/clockchips.h>
#include <linux/interrupt.h>
#include <linux/irq.h>

#include <linux/clk.h>
#include <linux/err.h>
#include <linux/ioport.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/atmel_tc.h>


/*
 * We're configured to use a specific TC block, one that's not hooked
 * up to external hardware, to provide a time solution:
 *
 *   - Two channels combine to create a free-running 32 bit counter
 *     with a base rate of 5+ MHz, packaged as a clocksource (with
 *     resolution better than 200 nsec).
 *
 *   - The third channel may be used to provide a 16-bit clockevent
 *     source, used in either periodic or oneshot mode.  This runs
 *     at 32 KiHZ, and can handle delays of up to two seconds.
 *
 * A boot clocksource and clockevent source are also currently needed,
 * unless the relevant platforms (ARM/AT91, AVR32/AT32) are changed so
 * this code can be used when init_timers() is called, well before most
 * devices are set up.  (Some low end AT91 parts, which can run uClinux,
 * have only the timers in one TC block... they currently don't support
 * the tclib code, because of that initialization issue.)
 *
 * REVISIT behavior during system suspend states... we should disable
 * all clocks and save the power.  Easily done for clockevent devices,
 * but clocksources won't necessarily get the needed notifications.
 * For deeper system sleep states, this will be mandatory...
 */

static void __iomem *tcaddr;

static cycle_t tc_get_cycles(struct clocksource *cs)
{
	unsigned long	flags;
	u32		lower, upper;

	raw_local_irq_save(flags);
	do {
		upper = __raw_readl(tcaddr + ATMEL_TC_REG(1, CV));
		lower = __raw_readl(tcaddr + ATMEL_TC_REG(0, CV));
	} while (upper != __raw_readl(tcaddr + ATMEL_TC_REG(1, CV)));

	raw_local_irq_restore(flags);
	return (upper << 16) | lower;
}

static struct clocksource clksrc = {
	.name           = "tcb_clksrc",
	.rating         = 200,
	.read           = tc_get_cycles,
	.mask           = CLOCKSOURCE_MASK(32),
	.flags		= CLOCK_SOURCE_IS_CONTINUOUS,
};

#ifdef CONFIG_GENERIC_CLOCKEVENTS

struct tc_clkevt_device {
	struct clock_event_device	clkevt;
	struct clk			*clk;
	void __iomem			*regs;
};

static struct tc_clkevt_device *to_tc_clkevt(struct clock_event_device *clkevt)
{
	return container_of(clkevt, struct tc_clkevt_device, clkevt);
}

/* For now, we always use the 32K clock ... this optimizes for NO_HZ,
 * because using one of the divided clocks would usually mean the
 * tick rate can never be less than several dozen Hz (vs 0.5 Hz).
 *
 * A divided clock could be good for high resolution timers, since
 * 30.5 usec resolution can seem "low".
 */
static u32 timer_clock;

static void tc_mode(enum clock_event_mode m, struct clock_event_device *d)
{
	struct tc_clkevt_device *tcd = to_tc_clkevt(d);
	void __iomem		*regs = tcd->regs;

	if (tcd->clkevt.mode == CLOCK_EVT_MODE_PERIODIC
			|| tcd->clkevt.mode == CLOCK_EVT_MODE_ONESHOT) {
		__raw_writel(0xff, regs + ATMEL_TC_REG(2, IDR));
		__raw_writel(ATMEL_TC_CLKDIS, regs + ATMEL_TC_REG(2, CCR));
		clk_disable(tcd->clk);
	}

	switch (m) {

	/* By not making the gentime core emulate periodic mode on top
	 * of oneshot, we get lower overhead and improved accuracy.
	 */
	case CLOCK_EVT_MODE_PERIODIC:
		clk_enable(tcd->clk);

		/* slow clock, count up to RC, then irq and restart */
		__raw_writel(timer_clock
				| ATMEL_TC_WAVE | ATMEL_TC_WAVESEL_UP_AUTO,
				regs + ATMEL_TC_REG(2, CMR));
		__raw_writel((32768 + HZ/2) / HZ, tcaddr + ATMEL_TC_REG(2, RC));

		/* Enable clock and interrupts on RC compare */
		__raw_writel(ATMEL_TC_CPCS, regs + ATMEL_TC_REG(2, IER));

		/* go go gadget! */
		__raw_writel(ATMEL_TC_CLKEN | ATMEL_TC_SWTRG,
				regs + ATMEL_TC_REG(2, CCR));
		break;

	case CLOCK_EVT_MODE_ONESHOT:
		clk_enable(tcd->clk);

		/* slow clock, count up to RC, then irq and stop */
		__raw_writel(timer_clock | ATMEL_TC_CPCSTOP
				| ATMEL_TC_WAVE | ATMEL_TC_WAVESEL_UP_AUTO,
				regs + ATMEL_TC_REG(2, CMR));
		__raw_writel(ATMEL_TC_CPCS, regs + ATMEL_TC_REG(2, IER));

		/* set_next_event() configures and starts the timer */
		break;

	default:
		break;
	}
}

static int tc_next_event(unsigned long delta, struct clock_event_device *d)
{
	__raw_writel(delta, tcaddr + ATMEL_TC_REG(2, RC));

	/* go go gadget! */
	__raw_writel(ATMEL_TC_CLKEN | ATMEL_TC_SWTRG,
			tcaddr + ATMEL_TC_REG(2, CCR));
	return 0;
}

static struct tc_clkevt_device clkevt = {
	.clkevt	= {
		.name		= "tc_clkevt",
		.features	= CLOCK_EVT_FEAT_PERIODIC
					| CLOCK_EVT_FEAT_ONESHOT,
		.shift		= 32,
		/* Should be lower than at91rm9200's system timer */
		.rating		= 125,
		.set_next_event	= tc_next_event,
		.set_mode	= tc_mode,
	},
};

static irqreturn_t ch2_irq(int irq, void *handle)
{
	struct tc_clkevt_device	*dev = handle;
	unsigned int		sr;

	sr = __raw_readl(dev->regs + ATMEL_TC_REG(2, SR));
	if (sr & ATMEL_TC_CPCS) {
		dev->clkevt.event_handler(&dev->clkevt);
		return IRQ_HANDLED;
	}

	return IRQ_NONE;
}

static struct irqaction tc_irqaction = {
	.name		= "tc_clkevt",
	.flags		= IRQF_TIMER | IRQF_DISABLED,
	.handler	= ch2_irq,
};

static void __init setup_clkevents(struct atmel_tc *tc, int clk32k_divisor_idx)
{
	struct clk *t2_clk = tc->clk[2];
	int irq = tc->irq[2];

	clkevt.regs = tc->regs;
	clkevt.clk = t2_clk;
	tc_irqaction.dev_id = &clkevt;

	timer_clock = clk32k_divisor_idx;

	clkevt.clkevt.mult = div_sc(32768, NSEC_PER_SEC, clkevt.clkevt.shift);
	clkevt.clkevt.max_delta_ns
		= clockevent_delta2ns(0xffff, &clkevt.clkevt);
	clkevt.clkevt.min_delta_ns = clockevent_delta2ns(1, &clkevt.clkevt) + 1;
	clkevt.clkevt.cpumask = cpumask_of(0);

	clockevents_register_device(&clkevt.clkevt);

	setup_irq(irq, &tc_irqaction);
}

#else /* !CONFIG_GENERIC_CLOCKEVENTS */

static void __init setup_clkevents(struct atmel_tc *tc, int clk32k_divisor_idx)
{
	/* NOTHING */
}

#endif

static int __init tcb_clksrc_init(void)
{
	static char bootinfo[] __initdata
		= KERN_DEBUG "%s: tc%d at %d.%03d MHz\n";

	struct platform_device *pdev;
	struct atmel_tc *tc;
	struct clk *t0_clk;
	u32 rate, divided_rate = 0;
	int best_divisor_idx = -1;
	int clk32k_divisor_idx = -1;
	int i;

	tc = atmel_tc_alloc(CONFIG_ATMEL_TCB_CLKSRC_BLOCK, clksrc.name);
	if (!tc) {
		pr_debug("can't alloc TC for clocksource\n");
		return -ENODEV;
	}
	tcaddr = tc->regs;
	pdev = tc->pdev;

	t0_clk = tc->clk[0];
	clk_enable(t0_clk);

	/* How fast will we be counting?  Pick something over 5 MHz.  */
	rate = (u32) clk_get_rate(t0_clk);
	for (i = 0; i < 5; i++) {
		unsigned divisor = atmel_tc_divisors[i];
		unsigned tmp;

		/* remember 32 KiHz clock for later */
		if (!divisor) {
			clk32k_divisor_idx = i;
			continue;
		}

		tmp = rate / divisor;
		pr_debug("TC: %u / %-3u [%d] --> %u\n", rate, divisor, i, tmp);
		if (best_divisor_idx > 0) {
			if (tmp < 5 * 1000 * 1000)
				continue;
		}
		divided_rate = tmp;
		best_divisor_idx = i;
	}


	printk(bootinfo, clksrc.name, CONFIG_ATMEL_TCB_CLKSRC_BLOCK,
			divided_rate / 1000000,
			((divided_rate + 500000) % 1000000) / 1000);

	/* tclib will give us three clocks no matter what the
	 * underlying platform supports.
	 */
	clk_enable(tc->clk[1]);

	/* channel 0:  waveform mode, input mclk/8, clock TIOA0 on overflow */
	__raw_writel(best_divisor_idx			/* likely divide-by-8 */
			| ATMEL_TC_WAVE
			| ATMEL_TC_WAVESEL_UP		/* free-run */
			| ATMEL_TC_ACPA_SET		/* TIOA0 rises at 0 */
			| ATMEL_TC_ACPC_CLEAR,		/* (duty cycle 50%) */
			tcaddr + ATMEL_TC_REG(0, CMR));
	__raw_writel(0x0000, tcaddr + ATMEL_TC_REG(0, RA));
	__raw_writel(0x8000, tcaddr + ATMEL_TC_REG(0, RC));
	__raw_writel(0xff, tcaddr + ATMEL_TC_REG(0, IDR));	/* no irqs */
	__raw_writel(ATMEL_TC_CLKEN, tcaddr + ATMEL_TC_REG(0, CCR));

	/* channel 1:  waveform mode, input TIOA0 */
	__raw_writel(ATMEL_TC_XC1			/* input: TIOA0 */
			| ATMEL_TC_WAVE
			| ATMEL_TC_WAVESEL_UP,		/* free-run */
			tcaddr + ATMEL_TC_REG(1, CMR));
	__raw_writel(0xff, tcaddr + ATMEL_TC_REG(1, IDR));	/* no irqs */
	__raw_writel(ATMEL_TC_CLKEN, tcaddr + ATMEL_TC_REG(1, CCR));

	/* chain channel 0 to channel 1, then reset all the timers */
	__raw_writel(ATMEL_TC_TC1XC1S_TIOA0, tcaddr + ATMEL_TC_BMR);
	__raw_writel(ATMEL_TC_SYNC, tcaddr + ATMEL_TC_BCR);

	/* and away we go! */
	clocksource_register_hz(&clksrc, divided_rate);

	/* channel 2:  periodic and oneshot timer support */
	setup_clkevents(tc, clk32k_divisor_idx);

	return 0;
}
arch_initcall(tcb_clksrc_init);
