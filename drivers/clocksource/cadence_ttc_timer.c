/*
 * This file contains driver for the Cadence Triple Timer Counter Rev 06
 *
 *  Copyright (C) 2011-2013 Xilinx
 *
 * based on arch/mips/kernel/time.c timer driver
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/clk.h>
#include <linux/interrupt.h>
#include <linux/clockchips.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/slab.h>
#include <linux/sched_clock.h>

/*
 * This driver configures the 2 16-bit count-up timers as follows:
 *
 * T1: Timer 1, clocksource for generic timekeeping
 * T2: Timer 2, clockevent source for hrtimers
 * T3: Timer 3, <unused>
 *
 * The input frequency to the timer module for emulation is 2.5MHz which is
 * common to all the timer channels (T1, T2, and T3). With a pre-scaler of 32,
 * the timers are clocked at 78.125KHz (12.8 us resolution).

 * The input frequency to the timer module in silicon is configurable and
 * obtained from device tree. The pre-scaler of 32 is used.
 */

/*
 * Timer Register Offset Definitions of Timer 1, Increment base address by 4
 * and use same offsets for Timer 2
 */
#define TTC_CLK_CNTRL_OFFSET		0x00 /* Clock Control Reg, RW */
#define TTC_CNT_CNTRL_OFFSET		0x0C /* Counter Control Reg, RW */
#define TTC_COUNT_VAL_OFFSET		0x18 /* Counter Value Reg, RO */
#define TTC_INTR_VAL_OFFSET		0x24 /* Interval Count Reg, RW */
#define TTC_ISR_OFFSET		0x54 /* Interrupt Status Reg, RO */
#define TTC_IER_OFFSET		0x60 /* Interrupt Enable Reg, RW */

#define TTC_CNT_CNTRL_DISABLE_MASK	0x1

#define TTC_CLK_CNTRL_CSRC_MASK		(1 << 5)	/* clock source */

/*
 * Setup the timers to use pre-scaling, using a fixed value for now that will
 * work across most input frequency, but it may need to be more dynamic
 */
#define PRESCALE_EXPONENT	11	/* 2 ^ PRESCALE_EXPONENT = PRESCALE */
#define PRESCALE		2048	/* The exponent must match this */
#define CLK_CNTRL_PRESCALE	((PRESCALE_EXPONENT - 1) << 1)
#define CLK_CNTRL_PRESCALE_EN	1
#define CNT_CNTRL_RESET		(1 << 4)

/**
 * struct ttc_timer - This definition defines local timer structure
 *
 * @base_addr:	Base address of timer
 * @freq:	Timer input clock frequency
 * @clk:	Associated clock source
 * @clk_rate_change_nb	Notifier block for clock rate changes
 */
struct ttc_timer {
	void __iomem *base_addr;
	unsigned long freq;
	struct clk *clk;
	struct notifier_block clk_rate_change_nb;
};

#define to_ttc_timer(x) \
		container_of(x, struct ttc_timer, clk_rate_change_nb)

struct ttc_timer_clocksource {
	struct ttc_timer	ttc;
	struct clocksource	cs;
};

#define to_ttc_timer_clksrc(x) \
		container_of(x, struct ttc_timer_clocksource, cs)

struct ttc_timer_clockevent {
	struct ttc_timer		ttc;
	struct clock_event_device	ce;
};

#define to_ttc_timer_clkevent(x) \
		container_of(x, struct ttc_timer_clockevent, ce)

static void __iomem *ttc_sched_clock_val_reg;

/**
 * ttc_set_interval - Set the timer interval value
 *
 * @timer:	Pointer to the timer instance
 * @cycles:	Timer interval ticks
 **/
static void ttc_set_interval(struct ttc_timer *timer,
					unsigned long cycles)
{
	u32 ctrl_reg;

	/* Disable the counter, set the counter value  and re-enable counter */
	ctrl_reg = __raw_readl(timer->base_addr + TTC_CNT_CNTRL_OFFSET);
	ctrl_reg |= TTC_CNT_CNTRL_DISABLE_MASK;
	__raw_writel(ctrl_reg, timer->base_addr + TTC_CNT_CNTRL_OFFSET);

	__raw_writel(cycles, timer->base_addr + TTC_INTR_VAL_OFFSET);

	/*
	 * Reset the counter (0x10) so that it starts from 0, one-shot
	 * mode makes this needed for timing to be right.
	 */
	ctrl_reg |= CNT_CNTRL_RESET;
	ctrl_reg &= ~TTC_CNT_CNTRL_DISABLE_MASK;
	__raw_writel(ctrl_reg, timer->base_addr + TTC_CNT_CNTRL_OFFSET);
}

/**
 * ttc_clock_event_interrupt - Clock event timer interrupt handler
 *
 * @irq:	IRQ number of the Timer
 * @dev_id:	void pointer to the ttc_timer instance
 *
 * returns: Always IRQ_HANDLED - success
 **/
static irqreturn_t ttc_clock_event_interrupt(int irq, void *dev_id)
{
	struct ttc_timer_clockevent *ttce = dev_id;
	struct ttc_timer *timer = &ttce->ttc;

	/* Acknowledge the interrupt and call event handler */
	__raw_readl(timer->base_addr + TTC_ISR_OFFSET);

	ttce->ce.event_handler(&ttce->ce);

	return IRQ_HANDLED;
}

/**
 * __ttc_clocksource_read - Reads the timer counter register
 *
 * returns: Current timer counter register value
 **/
static cycle_t __ttc_clocksource_read(struct clocksource *cs)
{
	struct ttc_timer *timer = &to_ttc_timer_clksrc(cs)->ttc;

	return (cycle_t)__raw_readl(timer->base_addr +
				TTC_COUNT_VAL_OFFSET);
}

static u64 notrace ttc_sched_clock_read(void)
{
	return __raw_readl(ttc_sched_clock_val_reg);
}

/**
 * ttc_set_next_event - Sets the time interval for next event
 *
 * @cycles:	Timer interval ticks
 * @evt:	Address of clock event instance
 *
 * returns: Always 0 - success
 **/
static int ttc_set_next_event(unsigned long cycles,
					struct clock_event_device *evt)
{
	struct ttc_timer_clockevent *ttce = to_ttc_timer_clkevent(evt);
	struct ttc_timer *timer = &ttce->ttc;

	ttc_set_interval(timer, cycles);
	return 0;
}

/**
 * ttc_set_mode - Sets the mode of timer
 *
 * @mode:	Mode to be set
 * @evt:	Address of clock event instance
 **/
static void ttc_set_mode(enum clock_event_mode mode,
					struct clock_event_device *evt)
{
	struct ttc_timer_clockevent *ttce = to_ttc_timer_clkevent(evt);
	struct ttc_timer *timer = &ttce->ttc;
	u32 ctrl_reg;

	switch (mode) {
	case CLOCK_EVT_MODE_PERIODIC:
		ttc_set_interval(timer, DIV_ROUND_CLOSEST(ttce->ttc.freq,
						PRESCALE * HZ));
		break;
	case CLOCK_EVT_MODE_ONESHOT:
	case CLOCK_EVT_MODE_UNUSED:
	case CLOCK_EVT_MODE_SHUTDOWN:
		ctrl_reg = __raw_readl(timer->base_addr +
					TTC_CNT_CNTRL_OFFSET);
		ctrl_reg |= TTC_CNT_CNTRL_DISABLE_MASK;
		__raw_writel(ctrl_reg,
				timer->base_addr + TTC_CNT_CNTRL_OFFSET);
		break;
	case CLOCK_EVT_MODE_RESUME:
		ctrl_reg = __raw_readl(timer->base_addr +
					TTC_CNT_CNTRL_OFFSET);
		ctrl_reg &= ~TTC_CNT_CNTRL_DISABLE_MASK;
		__raw_writel(ctrl_reg,
				timer->base_addr + TTC_CNT_CNTRL_OFFSET);
		break;
	}
}

static int ttc_rate_change_clocksource_cb(struct notifier_block *nb,
		unsigned long event, void *data)
{
	struct clk_notifier_data *ndata = data;
	struct ttc_timer *ttc = to_ttc_timer(nb);
	struct ttc_timer_clocksource *ttccs = container_of(ttc,
			struct ttc_timer_clocksource, ttc);

	switch (event) {
	case POST_RATE_CHANGE:
		/*
		 * Do whatever is necessary to maintain a proper time base
		 *
		 * I cannot find a way to adjust the currently used clocksource
		 * to the new frequency. __clocksource_updatefreq_hz() sounds
		 * good, but does not work. Not sure what's that missing.
		 *
		 * This approach works, but triggers two clocksource switches.
		 * The first after unregister to clocksource jiffies. And
		 * another one after the register to the newly registered timer.
		 *
		 * Alternatively we could 'waste' another HW timer to ping pong
		 * between clock sources. That would also use one register and
		 * one unregister call, but only trigger one clocksource switch
		 * for the cost of another HW timer used by the OS.
		 */
		clocksource_unregister(&ttccs->cs);
		clocksource_register_hz(&ttccs->cs,
				ndata->new_rate / PRESCALE);
		/* fall through */
	case PRE_RATE_CHANGE:
	case ABORT_RATE_CHANGE:
	default:
		return NOTIFY_DONE;
	}
}

static void __init ttc_setup_clocksource(struct clk *clk, void __iomem *base)
{
	struct ttc_timer_clocksource *ttccs;
	int err;

	ttccs = kzalloc(sizeof(*ttccs), GFP_KERNEL);
	if (WARN_ON(!ttccs))
		return;

	ttccs->ttc.clk = clk;

	err = clk_prepare_enable(ttccs->ttc.clk);
	if (WARN_ON(err)) {
		kfree(ttccs);
		return;
	}

	ttccs->ttc.freq = clk_get_rate(ttccs->ttc.clk);

	ttccs->ttc.clk_rate_change_nb.notifier_call =
		ttc_rate_change_clocksource_cb;
	ttccs->ttc.clk_rate_change_nb.next = NULL;
	if (clk_notifier_register(ttccs->ttc.clk,
				&ttccs->ttc.clk_rate_change_nb))
		pr_warn("Unable to register clock notifier.\n");

	ttccs->ttc.base_addr = base;
	ttccs->cs.name = "ttc_clocksource";
	ttccs->cs.rating = 200;
	ttccs->cs.read = __ttc_clocksource_read;
	ttccs->cs.mask = CLOCKSOURCE_MASK(16);
	ttccs->cs.flags = CLOCK_SOURCE_IS_CONTINUOUS;

	/*
	 * Setup the clock source counter to be an incrementing counter
	 * with no interrupt and it rolls over at 0xFFFF. Pre-scale
	 * it by 32 also. Let it start running now.
	 */
	__raw_writel(0x0,  ttccs->ttc.base_addr + TTC_IER_OFFSET);
	__raw_writel(CLK_CNTRL_PRESCALE | CLK_CNTRL_PRESCALE_EN,
		     ttccs->ttc.base_addr + TTC_CLK_CNTRL_OFFSET);
	__raw_writel(CNT_CNTRL_RESET,
		     ttccs->ttc.base_addr + TTC_CNT_CNTRL_OFFSET);

	err = clocksource_register_hz(&ttccs->cs, ttccs->ttc.freq / PRESCALE);
	if (WARN_ON(err)) {
		kfree(ttccs);
		return;
	}

	ttc_sched_clock_val_reg = base + TTC_COUNT_VAL_OFFSET;
	sched_clock_register(ttc_sched_clock_read, 16, ttccs->ttc.freq / PRESCALE);
}

static int ttc_rate_change_clockevent_cb(struct notifier_block *nb,
		unsigned long event, void *data)
{
	struct clk_notifier_data *ndata = data;
	struct ttc_timer *ttc = to_ttc_timer(nb);
	struct ttc_timer_clockevent *ttcce = container_of(ttc,
			struct ttc_timer_clockevent, ttc);

	switch (event) {
	case POST_RATE_CHANGE:
	{
		unsigned long flags;

		/*
		 * clockevents_update_freq should be called with IRQ disabled on
		 * the CPU the timer provides events for. The timer we use is
		 * common to both CPUs, not sure if we need to run on both
		 * cores.
		 */
		local_irq_save(flags);
		clockevents_update_freq(&ttcce->ce,
				ndata->new_rate / PRESCALE);
		local_irq_restore(flags);

		/* update cached frequency */
		ttc->freq = ndata->new_rate;

		/* fall through */
	}
	case PRE_RATE_CHANGE:
	case ABORT_RATE_CHANGE:
	default:
		return NOTIFY_DONE;
	}
}

static void __init ttc_setup_clockevent(struct clk *clk,
						void __iomem *base, u32 irq)
{
	struct ttc_timer_clockevent *ttcce;
	int err;

	ttcce = kzalloc(sizeof(*ttcce), GFP_KERNEL);
	if (WARN_ON(!ttcce))
		return;

	ttcce->ttc.clk = clk;

	err = clk_prepare_enable(ttcce->ttc.clk);
	if (WARN_ON(err)) {
		kfree(ttcce);
		return;
	}

	ttcce->ttc.clk_rate_change_nb.notifier_call =
		ttc_rate_change_clockevent_cb;
	ttcce->ttc.clk_rate_change_nb.next = NULL;
	if (clk_notifier_register(ttcce->ttc.clk,
				&ttcce->ttc.clk_rate_change_nb))
		pr_warn("Unable to register clock notifier.\n");
	ttcce->ttc.freq = clk_get_rate(ttcce->ttc.clk);

	ttcce->ttc.base_addr = base;
	ttcce->ce.name = "ttc_clockevent";
	ttcce->ce.features = CLOCK_EVT_FEAT_PERIODIC | CLOCK_EVT_FEAT_ONESHOT;
	ttcce->ce.set_next_event = ttc_set_next_event;
	ttcce->ce.set_mode = ttc_set_mode;
	ttcce->ce.rating = 200;
	ttcce->ce.irq = irq;
	ttcce->ce.cpumask = cpu_possible_mask;

	/*
	 * Setup the clock event timer to be an interval timer which
	 * is prescaled by 32 using the interval interrupt. Leave it
	 * disabled for now.
	 */
	__raw_writel(0x23, ttcce->ttc.base_addr + TTC_CNT_CNTRL_OFFSET);
	__raw_writel(CLK_CNTRL_PRESCALE | CLK_CNTRL_PRESCALE_EN,
		     ttcce->ttc.base_addr + TTC_CLK_CNTRL_OFFSET);
	__raw_writel(0x1,  ttcce->ttc.base_addr + TTC_IER_OFFSET);

	err = request_irq(irq, ttc_clock_event_interrupt,
			  IRQF_TIMER, ttcce->ce.name, ttcce);
	if (WARN_ON(err)) {
		kfree(ttcce);
		return;
	}

	clockevents_config_and_register(&ttcce->ce,
			ttcce->ttc.freq / PRESCALE, 1, 0xfffe);
}

/**
 * ttc_timer_init - Initialize the timer
 *
 * Initializes the timer hardware and register the clock source and clock event
 * timers with Linux kernal timer framework
 */
static void __init ttc_timer_init(struct device_node *timer)
{
	unsigned int irq;
	void __iomem *timer_baseaddr;
	struct clk *clk_cs, *clk_ce;
	static int initialized;
	int clksel;

	if (initialized)
		return;

	initialized = 1;

	/*
	 * Get the 1st Triple Timer Counter (TTC) block from the device tree
	 * and use it. Note that the event timer uses the interrupt and it's the
	 * 2nd TTC hence the irq_of_parse_and_map(,1)
	 */
	timer_baseaddr = of_iomap(timer, 0);
	if (!timer_baseaddr) {
		pr_err("ERROR: invalid timer base address\n");
		BUG();
	}

	irq = irq_of_parse_and_map(timer, 1);
	if (irq <= 0) {
		pr_err("ERROR: invalid interrupt number\n");
		BUG();
	}

	clksel = __raw_readl(timer_baseaddr + TTC_CLK_CNTRL_OFFSET);
	clksel = !!(clksel & TTC_CLK_CNTRL_CSRC_MASK);
	clk_cs = of_clk_get(timer, clksel);
	if (IS_ERR(clk_cs)) {
		pr_err("ERROR: timer input clock not found\n");
		BUG();
	}

	clksel = __raw_readl(timer_baseaddr + 4 + TTC_CLK_CNTRL_OFFSET);
	clksel = !!(clksel & TTC_CLK_CNTRL_CSRC_MASK);
	clk_ce = of_clk_get(timer, clksel);
	if (IS_ERR(clk_ce)) {
		pr_err("ERROR: timer input clock not found\n");
		BUG();
	}

	ttc_setup_clocksource(clk_cs, timer_baseaddr);
	ttc_setup_clockevent(clk_ce, timer_baseaddr + 4, irq);

	pr_info("%s #0 at %p, irq=%d\n", timer->name, timer_baseaddr, irq);
}

CLOCKSOURCE_OF_DECLARE(ttc, "cdns,ttc", ttc_timer_init);
