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
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/clockchips.h>
#include <linux/module.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/of.h>

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

struct timer8_priv {
	struct platform_device *pdev;
	struct clock_event_device ced;
	struct irqaction irqaction;
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
		dev_warn(&p->pdev->dev, "delta out of range\n");
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
	p->rate = clk_get_rate(p->pclk) / 64;
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

	dev_info(&p->pdev->dev, "used for periodic clock events\n");
	timer8_stop(p);
	timer8_clock_event_start(p, PERIODIC);

	return 0;
}

static int timer8_clock_event_oneshot(struct clock_event_device *ced)
{
	struct timer8_priv *p = ced_to_priv(ced);

	dev_info(&p->pdev->dev, "used for oneshot clock events\n");
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

static int timer8_setup(struct timer8_priv *p,
			struct platform_device *pdev)
{
	struct resource *res;
	int irq;
	int ret;

	p->pdev = pdev;

	res = platform_get_resource(p->pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&p->pdev->dev, "failed to get I/O memory\n");
		return -ENXIO;
	}

	irq = platform_get_irq(p->pdev, 0);
	if (irq < 0) {
		dev_err(&p->pdev->dev, "failed to get irq\n");
		return -ENXIO;
	}

	p->mapbase = res->start;

	p->irqaction.name = dev_name(&p->pdev->dev);
	p->irqaction.handler = timer8_interrupt;
	p->irqaction.dev_id = p;
	p->irqaction.flags = IRQF_TIMER;

	p->pclk = clk_get(&p->pdev->dev, "fck");
	if (IS_ERR(p->pclk)) {
		dev_err(&p->pdev->dev, "can't get clk\n");
		return PTR_ERR(p->pclk);
	}

	p->ced.name = pdev->name;
	p->ced.features = CLOCK_EVT_FEAT_PERIODIC |
		CLOCK_EVT_FEAT_ONESHOT;
	p->ced.rating = 200;
	p->ced.cpumask = cpumask_of(0);
	p->ced.set_next_event = timer8_clock_event_next;
	p->ced.set_state_shutdown = timer8_clock_event_shutdown;
	p->ced.set_state_periodic = timer8_clock_event_periodic;
	p->ced.set_state_oneshot = timer8_clock_event_oneshot;

	ret = setup_irq(irq, &p->irqaction);
	if (ret < 0) {
		dev_err(&p->pdev->dev,
			"failed to request irq %d\n", irq);
		return ret;
	}
	clockevents_register_device(&p->ced);
	platform_set_drvdata(pdev, p);

	return 0;
}

static int timer8_probe(struct platform_device *pdev)
{
	struct timer8_priv *p = platform_get_drvdata(pdev);

	if (p) {
		dev_info(&pdev->dev, "kept as earlytimer\n");
		return 0;
	}

	p = devm_kzalloc(&pdev->dev, sizeof(*p), GFP_KERNEL);
	if (!p)
		return -ENOMEM;

	return timer8_setup(p, pdev);
}

static int timer8_remove(struct platform_device *pdev)
{
	return -EBUSY;
}

static const struct of_device_id timer8_of_table[] __maybe_unused = {
	{ .compatible = "renesas,8bit-timer" },
	{ }
};

MODULE_DEVICE_TABLE(of, timer8_of_table);
static struct platform_driver timer8_driver = {
	.probe		= timer8_probe,
	.remove		= timer8_remove,
	.driver		= {
		.name	= "h8300-8timer",
		.of_match_table = of_match_ptr(timer8_of_table),
	}
};

static int __init timer8_init(void)
{
	return platform_driver_register(&timer8_driver);
}

static void __exit timer8_exit(void)
{
	platform_driver_unregister(&timer8_driver);
}

subsys_initcall(timer8_init);
module_exit(timer8_exit);
MODULE_AUTHOR("Yoshinori Sato");
MODULE_DESCRIPTION("H8/300 8bit Timer Driver");
MODULE_LICENSE("GPL v2");
