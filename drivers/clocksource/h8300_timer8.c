/*
 *  linux/arch/h8300/kernel/cpu/timer/timer8.c
 *
 *  Yoshinori Sato <ysato@users.sourcefoge.jp>
 *
 *  8bit Timer driver
 *
 */

#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/clockchips.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>

#define _8TCR	0
#define _8TCSR	2
#define TCORA	4
#define TCORB	6
#define _8TCNT	8

#define CMIEA	6
#define CMFA	6

#define FLAG_STARTED (1 << 3)

#define SCALE 64

#define bset(b, a) iowrite8(ioread8(a) | (1 << (b)), (a))
#define bclr(b, a) iowrite8(ioread8(a) & ~(1 << (b)), (a))

struct timer8_priv {
	struct clock_event_device ced;
	void __iomem *mapbase;
	unsigned long flags;
	unsigned int rate;
};

static irqreturn_t timer8_interrupt(int irq, void *dev_id)
{
	struct timer8_priv *p = dev_id;

	if (clockevent_state_oneshot(&p->ced))
		iowrite16be(0x0000, p->mapbase + _8TCR);

	p->ced.event_handler(&p->ced);

	bclr(CMFA, p->mapbase + _8TCSR);

	return IRQ_HANDLED;
}

static void timer8_set_next(struct timer8_priv *p, unsigned long delta)
{
	if (delta >= 0x10000)
		pr_warn("delta out of range\n");
	bclr(CMIEA, p->mapbase + _8TCR);
	iowrite16be(delta, p->mapbase + TCORA);
	iowrite16be(0x0000, p->mapbase + _8TCNT);
	bclr(CMFA, p->mapbase + _8TCSR);
	bset(CMIEA, p->mapbase + _8TCR);
}

static int timer8_enable(struct timer8_priv *p)
{
	iowrite16be(0xffff, p->mapbase + TCORA);
	iowrite16be(0x0000, p->mapbase + _8TCNT);
	iowrite16be(0x0c02, p->mapbase + _8TCR);

	return 0;
}

static int timer8_start(struct timer8_priv *p)
{
	int ret;

	if ((p->flags & FLAG_STARTED))
		return 0;

	ret = timer8_enable(p);
	if (!ret)
		p->flags |= FLAG_STARTED;

	return ret;
}

static void timer8_stop(struct timer8_priv *p)
{
	iowrite16be(0x0000, p->mapbase + _8TCR);
}

static inline struct timer8_priv *ced_to_priv(struct clock_event_device *ced)
{
	return container_of(ced, struct timer8_priv, ced);
}

static void timer8_clock_event_start(struct timer8_priv *p, unsigned long delta)
{
	timer8_start(p);
	timer8_set_next(p, delta);
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
	timer8_clock_event_start(p, (p->rate + HZ/2) / HZ);

	return 0;
}

static int timer8_clock_event_oneshot(struct clock_event_device *ced)
{
	struct timer8_priv *p = ced_to_priv(ced);

	pr_info("%s: used for oneshot clock events\n", ced->name);
	timer8_stop(p);
	timer8_clock_event_start(p, 0x10000);

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

static int __init h8300_8timer_init(struct device_node *node)
{
	void __iomem *base;
	int irq, ret;
	struct clk *clk;

	clk = of_clk_get(node, 0);
	if (IS_ERR(clk)) {
		pr_err("failed to get clock for clockevent\n");
		return PTR_ERR(clk);
	}

	ret = ENXIO;
	base = of_iomap(node, 0);
	if (!base) {
		pr_err("failed to map registers for clockevent\n");
		goto free_clk;
	}

	ret = -EINVAL;
	irq = irq_of_parse_and_map(node, 0);
	if (!irq) {
		pr_err("failed to get irq for clockevent\n");
		goto unmap_reg;
	}

	timer8_priv.mapbase = base;

	timer8_priv.rate = clk_get_rate(clk) / SCALE;
	if (!timer8_priv.rate) {
		pr_err("Failed to get rate for the clocksource\n");
		goto unmap_reg;
	}

	if (request_irq(irq, timer8_interrupt, IRQF_TIMER,
			timer8_priv.ced.name, &timer8_priv) < 0) {
		pr_err("failed to request irq %d for clockevent\n", irq);
		goto unmap_reg;
	}

	clockevents_config_and_register(&timer8_priv.ced,
					timer8_priv.rate, 1, 0x0000ffff);

	return 0;
unmap_reg:
	iounmap(base);
free_clk:
	clk_put(clk);
	return ret;
}

TIMER_OF_DECLARE(h8300_8bit, "renesas,8bit-timer", h8300_8timer_init);
