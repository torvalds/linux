/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __TIMER_OF_H__
#define __TIMER_OF_H__

#include <linux/clockchips.h>

#define TIMER_OF_BASE	0x1
#define TIMER_OF_CLOCK	0x2
#define TIMER_OF_IRQ	0x4

struct of_timer_irq {
	int irq;
	int index;
	int percpu;
	const char *name;
	unsigned long flags;
	irq_handler_t handler;
};

struct of_timer_base {
	void __iomem *base;
	const char *name;
	int index;
};

struct of_timer_clk {
	struct clk *clk;
	const char *name;
	int index;
	unsigned long rate;
	unsigned long period;
};

struct timer_of {
	unsigned int flags;
	struct clock_event_device clkevt;
	struct of_timer_base of_base;
	struct of_timer_irq  of_irq;
	struct of_timer_clk  of_clk;
	void *private_data;
};

static inline struct timer_of *to_timer_of(struct clock_event_device *clkevt)
{
	return container_of(clkevt, struct timer_of, clkevt);
}

static inline void __iomem *timer_of_base(struct timer_of *to)
{
	return to->of_base.base;
}

static inline int timer_of_irq(struct timer_of *to)
{
	return to->of_irq.irq;
}

static inline unsigned long timer_of_rate(struct timer_of *to)
{
	return to->of_clk.rate;
}

static inline unsigned long timer_of_period(struct timer_of *to)
{
	return to->of_clk.period;
}

extern int __init timer_of_init(struct device_node *np,
				struct timer_of *to);

extern void __init timer_of_cleanup(struct timer_of *to);

#endif
