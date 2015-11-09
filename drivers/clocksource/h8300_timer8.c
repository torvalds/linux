/*
 *  linux/arch/h8300/kernel/cpu/timer/timer8.c
 *
 *  Yoshinori Sato <ysato@users.sourcefoge.jp>
 *
 *  8bit Timer driver
 *
 */

#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/clockchips.h>
#include <linux/module.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>

#include <asm/irq.h>

#define _8TCR	0
#define _8TCSR	2
#define TCORA	4
#define TCORB	6
#define _8TCNT	8

#define FLAG_REPROGRAM (1 << 0)
#define FLAG_SKIPEVENT (1 << 1)
#define FLAG_IRQCONTEXT (1 << 2)
#define FLAG_STARTED (1 << 3)

#define ONESHOT  0
#define PERIODIC 1

#define RELATIVE 0
#define ABSOLUTE 1

#define SCALE 64

struct timer8_priv {
	struct clock_event_device ced;
	unsigned long mapbase;
	raw_spinlock_t lock;
	unsigned long flags;
	unsigned int rate;
	unsigned int tcora;
	struct clk *pclk;
};

static unsigned long timer8_get_counter(struct timer8_priv *p)
{
	unsigned long v1, v2, v3;
	int o1, o2;

	o1 = ctrl_inb(p->mapbase + _8TCSR) & 0x20;

	/* Make sure the timer value is stable. Stolen from acpi_pm.c */
	do {
		o2 = o1;
		v1 = ctrl_inw(p->mapbase + _8TCNT);
		v2 = ctrl_inw(p->mapbase + _8TCNT);
		v3 = ctrl_inw(p->mapbase + _8TCNT);
		o1 = ctrl_inb(p->mapbase + _8TCSR) & 0x20;
	} while (unlikely((o1 != o2) || (v1 > v2 && v1 < v3)
			  || (v2 > v3 && v2 < v1) || (v3 > v1 && v3 < v2)));

	v2 |= o1 << 10;
	return v2;
}

static irqreturn_t timer8_interrupt(int irq, void *dev_id)
{
	struct timer8_priv *p = dev_id;

	ctrl_outb(ctrl_inb(p->mapbase + _8TCSR) & ~0x40,
		  p->mapbase + _8TCSR);
	p->flags |= FLAG_IRQCONTEXT;
	ctrl_outw(p->tcora, p->mapbase + TCORA);
	if (!(p->flags & FLAG_SKIPEVENT)) {
		if (clockevent_state_oneshot(&p->ced))
			ctrl_outw(0x0000, p->mapbase + _8TCR);
		p->ced.event_handler(&p->ced);
	}
	p->flags &= ~(FLAG_SKIPEVENT | FLAG_IRQCONTEXT);

	return IRQ_HANDLED;
}

static void timer8_set_next(struct timer8_priv *p, unsigned long delta)
{
	unsigned long flags;
	unsigned long now;

	raw_spin_lock_irqsave(&p->lock, flags);
	if (delta >= 0x10000)
		pr_warn("delta out of range\n");
	now = timer8_get_counter(p);
	p->tcora = delta;
	ctrl_outb(ctrl_inb(p->mapbase + _8TCR) | 0x40, p->mapbase + _8TCR);
	if (delta > now)
		ctrl_outw(delta, p->mapbase + TCORA);
	else
		ctrl_outw(now + 1, p->mapbase + TCORA);

	raw_spin_unlock_irqrestore(&p->lock, flags);
}

static int timer8_enable(struct timer8_priv *p)
{
	p->rate = clk_get_rate(p->pclk) / SCALE;
	ctrl_outw(0xffff, p->mapbase + TCORA);
	ctrl_outw(0x0000, p->mapbase + _8TCNT);
	ctrl_outw(0x0c02, p->mapbase + _8TCR);

	return 0;
}

static int timer8_start(struct timer8_priv *p)
{
	int ret = 0;
	unsigned long flags;

	raw_spin_lock_irqsave(&p->lock, flags);

	if (!(p->flags & FLAG_STARTED))
		ret = timer8_enable(p);

	if (ret)
		goto out;
	p->flags |= FLAG_STARTED;

 out:
	raw_spin_unlock_irqrestore(&p->lock, flags);

	return ret;
}

static void timer8_stop(struct timer8_priv *p)
{
	unsigned long flags;

	raw_spin_lock_irqsave(&p->lock, flags);

	ctrl_outw(0x0000, p->mapbase + _8TCR);

	raw_spin_unlock_irqrestore(&p->lock, flags);
}

static inline struct timer8_priv *ced_to_priv(struct clock_event_device *ced)
{
	return container_of(ced, struct timer8_priv, ced);
}

static void timer8_clock_event_start(struct timer8_priv *p, int periodic)
{
	struct clock_event_device *ced = &p->ced;

	timer8_start(p);

	ced->shift = 32;
	ced->mult = div_sc(p->rate, NSEC_PER_SEC, ced->shift);
	ced->max_delta_ns = clockevent_delta2ns(0xffff, ced);
	ced->min_delta_ns = clockevent_delta2ns(0x0001, ced);

	timer8_set_next(p, periodic?(p->rate + HZ/2) / HZ:0x10000);
}

static int timer8_clock_event_shutdown(struct clock_event_device *ced)
{
	timer8_stop(ced_to_priv(ced));
	return 0;
}

static int timer8_clock_event_periodic(struct clock_event_device *ced)
{
	struct timer8_priv *p = ced_to_priv(ced);

	pr_info("%s: used for periodic clock events\n", ced->name);
	timer8_stop(p);
	timer8_clock_event_start(p, PERIODIC);

	return 0;
}

static int timer8_clock_event_oneshot(struct clock_event_device *ced)
{
	struct timer8_priv *p = ced_to_priv(ced);

	pr_info("%s: used for oneshot clock events\n", ced->name);
	timer8_stop(p);
	timer8_clock_event_start(p, ONESHOT);

	return 0;
}

static int timer8_clock_event_next(unsigned long delta,
				   struct clock_event_device *ced)
{
	struct timer8_priv *p = ced_to_priv(ced);

	BUG_ON(!clockevent_state_oneshot(ced));
	timer8_set_next(p, delta - 1);

	return 0;
}

static struct timer8_priv timer8_priv = {
	.ced = {
		.name = "h8300_8timer",
		.features = CLOCK_EVT_FEAT_PERIODIC | CLOCK_EVT_FEAT_ONESHOT,
		.rating = 200,
		.set_next_event = timer8_clock_event_next,
		.set_state_shutdown = timer8_clock_event_shutdown,
		.set_state_periodic = timer8_clock_event_periodic,
		.set_state_oneshot = timer8_clock_event_oneshot,
	},
};

static void __init h8300_8timer_init(struct device_node *node)
{
	void __iomem *base;
	int irq;
	int ret = 0;
	int rate;
	struct clk *clk;

	clk = of_clk_get(node, 0);
	if (IS_ERR(clk)) {
		pr_err("failed to get clock for clockevent\n");
		return;
	}

	base = of_iomap(node, 0);
	if (!base) {
		pr_err("failed to map registers for clockevent\n");
		goto free_clk;
	}

	irq = irq_of_parse_and_map(node, 0);
	if (irq < 0) {
		pr_err("failed to get irq for clockevent\n");
		goto unmap_reg;
	}

	timer8_priv.mapbase = (unsigned long)base;
	timer8_priv.pclk = clk;

	ret = request_irq(irq, timer8_interrupt,
			  IRQF_TIMER, timer8_priv.ced.name, &timer8_priv);
	if (ret < 0) {
		pr_err("failed to request irq %d for clockevent\n", irq);
		goto unmap_reg;
	}
	rate = clk_get_rate(clk) / SCALE;
	clockevents_config_and_register(&timer8_priv.ced, rate, 1, 0x0000ffff);
	return;

unmap_reg:
	iounmap(base);
free_clk:
	clk_put(clk);
}

CLOCKSOURCE_OF_DECLARE(h8300_8bit, "renesas,8bit-timer", h8300_8timer_init);
