/*
 * SuperH Timer Support - CMT
 *
 *  Copyright (C) 2008 Magnus Damm
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/io.h>
#include <linux/clk.h>
#include <linux/irq.h>
#include <linux/err.h>
#include <linux/clocksource.h>
#include <linux/clockchips.h>
#include <linux/sh_timer.h>
#include <linux/slab.h>

struct sh_cmt_priv {
	void __iomem *mapbase;
	struct clk *clk;
	unsigned long width; /* 16 or 32 bit version of hardware block */
	unsigned long overflow_bit;
	unsigned long clear_bits;
	struct irqaction irqaction;
	struct platform_device *pdev;

	unsigned long flags;
	unsigned long match_value;
	unsigned long next_match_value;
	unsigned long max_match_value;
	unsigned long rate;
	spinlock_t lock;
	struct clock_event_device ced;
	struct clocksource cs;
	unsigned long total_cycles;
};

static DEFINE_SPINLOCK(sh_cmt_lock);

#define CMSTR -1 /* shared register */
#define CMCSR 0 /* channel register */
#define CMCNT 1 /* channel register */
#define CMCOR 2 /* channel register */

static inline unsigned long sh_cmt_read(struct sh_cmt_priv *p, int reg_nr)
{
	struct sh_timer_config *cfg = p->pdev->dev.platform_data;
	void __iomem *base = p->mapbase;
	unsigned long offs;

	if (reg_nr == CMSTR) {
		offs = 0;
		base -= cfg->channel_offset;
	} else
		offs = reg_nr;

	if (p->width == 16)
		offs <<= 1;
	else {
		offs <<= 2;
		if ((reg_nr == CMCNT) || (reg_nr == CMCOR))
			return ioread32(base + offs);
	}

	return ioread16(base + offs);
}

static inline void sh_cmt_write(struct sh_cmt_priv *p, int reg_nr,
				unsigned long value)
{
	struct sh_timer_config *cfg = p->pdev->dev.platform_data;
	void __iomem *base = p->mapbase;
	unsigned long offs;

	if (reg_nr == CMSTR) {
		offs = 0;
		base -= cfg->channel_offset;
	} else
		offs = reg_nr;

	if (p->width == 16)
		offs <<= 1;
	else {
		offs <<= 2;
		if ((reg_nr == CMCNT) || (reg_nr == CMCOR)) {
			iowrite32(value, base + offs);
			return;
		}
	}

	iowrite16(value, base + offs);
}

static unsigned long sh_cmt_get_counter(struct sh_cmt_priv *p,
					int *has_wrapped)
{
	unsigned long v1, v2, v3;
	int o1, o2;

	o1 = sh_cmt_read(p, CMCSR) & p->overflow_bit;

	/* Make sure the timer value is stable. Stolen from acpi_pm.c */
	do {
		o2 = o1;
		v1 = sh_cmt_read(p, CMCNT);
		v2 = sh_cmt_read(p, CMCNT);
		v3 = sh_cmt_read(p, CMCNT);
		o1 = sh_cmt_read(p, CMCSR) & p->overflow_bit;
	} while (unlikely((o1 != o2) || (v1 > v2 && v1 < v3)
			  || (v2 > v3 && v2 < v1) || (v3 > v1 && v3 < v2)));

	*has_wrapped = o1;
	return v2;
}


static void sh_cmt_start_stop_ch(struct sh_cmt_priv *p, int start)
{
	struct sh_timer_config *cfg = p->pdev->dev.platform_data;
	unsigned long flags, value;

	/* start stop register shared by multiple timer channels */
	spin_lock_irqsave(&sh_cmt_lock, flags);
	value = sh_cmt_read(p, CMSTR);

	if (start)
		value |= 1 << cfg->timer_bit;
	else
		value &= ~(1 << cfg->timer_bit);

	sh_cmt_write(p, CMSTR, value);
	spin_unlock_irqrestore(&sh_cmt_lock, flags);
}

static int sh_cmt_enable(struct sh_cmt_priv *p, unsigned long *rate)
{
	int ret;

	/* enable clock */
	ret = clk_enable(p->clk);
	if (ret) {
		dev_err(&p->pdev->dev, "cannot enable clock\n");
		return ret;
	}

	/* make sure channel is disabled */
	sh_cmt_start_stop_ch(p, 0);

	/* configure channel, periodic mode and maximum timeout */
	if (p->width == 16) {
		*rate = clk_get_rate(p->clk) / 512;
		sh_cmt_write(p, CMCSR, 0x43);
	} else {
		*rate = clk_get_rate(p->clk) / 8;
		sh_cmt_write(p, CMCSR, 0x01a4);
	}

	sh_cmt_write(p, CMCOR, 0xffffffff);
	sh_cmt_write(p, CMCNT, 0);

	/* enable channel */
	sh_cmt_start_stop_ch(p, 1);
	return 0;
}

static void sh_cmt_disable(struct sh_cmt_priv *p)
{
	/* disable channel */
	sh_cmt_start_stop_ch(p, 0);

	/* disable interrupts in CMT block */
	sh_cmt_write(p, CMCSR, 0);

	/* stop clock */
	clk_disable(p->clk);
}

/* private flags */
#define FLAG_CLOCKEVENT (1 << 0)
#define FLAG_CLOCKSOURCE (1 << 1)
#define FLAG_REPROGRAM (1 << 2)
#define FLAG_SKIPEVENT (1 << 3)
#define FLAG_IRQCONTEXT (1 << 4)

static void sh_cmt_clock_event_program_verify(struct sh_cmt_priv *p,
					      int absolute)
{
	unsigned long new_match;
	unsigned long value = p->next_match_value;
	unsigned long delay = 0;
	unsigned long now = 0;
	int has_wrapped;

	now = sh_cmt_get_counter(p, &has_wrapped);
	p->flags |= FLAG_REPROGRAM; /* force reprogram */

	if (has_wrapped) {
		/* we're competing with the interrupt handler.
		 *  -> let the interrupt handler reprogram the timer.
		 *  -> interrupt number two handles the event.
		 */
		p->flags |= FLAG_SKIPEVENT;
		return;
	}

	if (absolute)
		now = 0;

	do {
		/* reprogram the timer hardware,
		 * but don't save the new match value yet.
		 */
		new_match = now + value + delay;
		if (new_match > p->max_match_value)
			new_match = p->max_match_value;

		sh_cmt_write(p, CMCOR, new_match);

		now = sh_cmt_get_counter(p, &has_wrapped);
		if (has_wrapped && (new_match > p->match_value)) {
			/* we are changing to a greater match value,
			 * so this wrap must be caused by the counter
			 * matching the old value.
			 * -> first interrupt reprograms the timer.
			 * -> interrupt number two handles the event.
			 */
			p->flags |= FLAG_SKIPEVENT;
			break;
		}

		if (has_wrapped) {
			/* we are changing to a smaller match value,
			 * so the wrap must be caused by the counter
			 * matching the new value.
			 * -> save programmed match value.
			 * -> let isr handle the event.
			 */
			p->match_value = new_match;
			break;
		}

		/* be safe: verify hardware settings */
		if (now < new_match) {
			/* timer value is below match value, all good.
			 * this makes sure we won't miss any match events.
			 * -> save programmed match value.
			 * -> let isr handle the event.
			 */
			p->match_value = new_match;
			break;
		}

		/* the counter has reached a value greater
		 * than our new match value. and since the
		 * has_wrapped flag isn't set we must have
		 * programmed a too close event.
		 * -> increase delay and retry.
		 */
		if (delay)
			delay <<= 1;
		else
			delay = 1;

		if (!delay)
			dev_warn(&p->pdev->dev, "too long delay\n");

	} while (delay);
}

static void sh_cmt_set_next(struct sh_cmt_priv *p, unsigned long delta)
{
	unsigned long flags;

	if (delta > p->max_match_value)
		dev_warn(&p->pdev->dev, "delta out of range\n");

	spin_lock_irqsave(&p->lock, flags);
	p->next_match_value = delta;
	sh_cmt_clock_event_program_verify(p, 0);
	spin_unlock_irqrestore(&p->lock, flags);
}

static irqreturn_t sh_cmt_interrupt(int irq, void *dev_id)
{
	struct sh_cmt_priv *p = dev_id;

	/* clear flags */
	sh_cmt_write(p, CMCSR, sh_cmt_read(p, CMCSR) & p->clear_bits);

	/* update clock source counter to begin with if enabled
	 * the wrap flag should be cleared by the timer specific
	 * isr before we end up here.
	 */
	if (p->flags & FLAG_CLOCKSOURCE)
		p->total_cycles += p->match_value + 1;

	if (!(p->flags & FLAG_REPROGRAM))
		p->next_match_value = p->max_match_value;

	p->flags |= FLAG_IRQCONTEXT;

	if (p->flags & FLAG_CLOCKEVENT) {
		if (!(p->flags & FLAG_SKIPEVENT)) {
			if (p->ced.mode == CLOCK_EVT_MODE_ONESHOT) {
				p->next_match_value = p->max_match_value;
				p->flags |= FLAG_REPROGRAM;
			}

			p->ced.event_handler(&p->ced);
		}
	}

	p->flags &= ~FLAG_SKIPEVENT;

	if (p->flags & FLAG_REPROGRAM) {
		p->flags &= ~FLAG_REPROGRAM;
		sh_cmt_clock_event_program_verify(p, 1);

		if (p->flags & FLAG_CLOCKEVENT)
			if ((p->ced.mode == CLOCK_EVT_MODE_SHUTDOWN)
			    || (p->match_value == p->next_match_value))
				p->flags &= ~FLAG_REPROGRAM;
	}

	p->flags &= ~FLAG_IRQCONTEXT;

	return IRQ_HANDLED;
}

static int sh_cmt_start(struct sh_cmt_priv *p, unsigned long flag)
{
	int ret = 0;
	unsigned long flags;

	spin_lock_irqsave(&p->lock, flags);

	if (!(p->flags & (FLAG_CLOCKEVENT | FLAG_CLOCKSOURCE)))
		ret = sh_cmt_enable(p, &p->rate);

	if (ret)
		goto out;
	p->flags |= flag;

	/* setup timeout if no clockevent */
	if ((flag == FLAG_CLOCKSOURCE) && (!(p->flags & FLAG_CLOCKEVENT)))
		sh_cmt_set_next(p, p->max_match_value);
 out:
	spin_unlock_irqrestore(&p->lock, flags);

	return ret;
}

static void sh_cmt_stop(struct sh_cmt_priv *p, unsigned long flag)
{
	unsigned long flags;
	unsigned long f;

	spin_lock_irqsave(&p->lock, flags);

	f = p->flags & (FLAG_CLOCKEVENT | FLAG_CLOCKSOURCE);
	p->flags &= ~flag;

	if (f && !(p->flags & (FLAG_CLOCKEVENT | FLAG_CLOCKSOURCE)))
		sh_cmt_disable(p);

	/* adjust the timeout to maximum if only clocksource left */
	if ((flag == FLAG_CLOCKEVENT) && (p->flags & FLAG_CLOCKSOURCE))
		sh_cmt_set_next(p, p->max_match_value);

	spin_unlock_irqrestore(&p->lock, flags);
}

static struct sh_cmt_priv *cs_to_sh_cmt(struct clocksource *cs)
{
	return container_of(cs, struct sh_cmt_priv, cs);
}

static cycle_t sh_cmt_clocksource_read(struct clocksource *cs)
{
	struct sh_cmt_priv *p = cs_to_sh_cmt(cs);
	unsigned long flags, raw;
	unsigned long value;
	int has_wrapped;

	spin_lock_irqsave(&p->lock, flags);
	value = p->total_cycles;
	raw = sh_cmt_get_counter(p, &has_wrapped);

	if (unlikely(has_wrapped))
		raw += p->match_value + 1;
	spin_unlock_irqrestore(&p->lock, flags);

	return value + raw;
}

static int sh_cmt_clocksource_enable(struct clocksource *cs)
{
	struct sh_cmt_priv *p = cs_to_sh_cmt(cs);

	p->total_cycles = 0;

	return sh_cmt_start(p, FLAG_CLOCKSOURCE);
}

static void sh_cmt_clocksource_disable(struct clocksource *cs)
{
	sh_cmt_stop(cs_to_sh_cmt(cs), FLAG_CLOCKSOURCE);
}

static void sh_cmt_clocksource_resume(struct clocksource *cs)
{
	sh_cmt_start(cs_to_sh_cmt(cs), FLAG_CLOCKSOURCE);
}

static int sh_cmt_register_clocksource(struct sh_cmt_priv *p,
				       char *name, unsigned long rating)
{
	struct clocksource *cs = &p->cs;

	cs->name = name;
	cs->rating = rating;
	cs->read = sh_cmt_clocksource_read;
	cs->enable = sh_cmt_clocksource_enable;
	cs->disable = sh_cmt_clocksource_disable;
	cs->suspend = sh_cmt_clocksource_disable;
	cs->resume = sh_cmt_clocksource_resume;
	cs->mask = CLOCKSOURCE_MASK(sizeof(unsigned long) * 8);
	cs->flags = CLOCK_SOURCE_IS_CONTINUOUS;

	/* clk_get_rate() needs an enabled clock */
	clk_enable(p->clk);
	p->rate = clk_get_rate(p->clk) / ((p->width == 16) ? 512 : 8);
	clk_disable(p->clk);

	/* TODO: calculate good shift from rate and counter bit width */
	cs->shift = 0;
	cs->mult = clocksource_hz2mult(p->rate, cs->shift);

	dev_info(&p->pdev->dev, "used as clock source\n");

	clocksource_register(cs);

	return 0;
}

static struct sh_cmt_priv *ced_to_sh_cmt(struct clock_event_device *ced)
{
	return container_of(ced, struct sh_cmt_priv, ced);
}

static void sh_cmt_clock_event_start(struct sh_cmt_priv *p, int periodic)
{
	struct clock_event_device *ced = &p->ced;

	sh_cmt_start(p, FLAG_CLOCKEVENT);

	/* TODO: calculate good shift from rate and counter bit width */

	ced->shift = 32;
	ced->mult = div_sc(p->rate, NSEC_PER_SEC, ced->shift);
	ced->max_delta_ns = clockevent_delta2ns(p->max_match_value, ced);
	ced->min_delta_ns = clockevent_delta2ns(0x1f, ced);

	if (periodic)
		sh_cmt_set_next(p, ((p->rate + HZ/2) / HZ) - 1);
	else
		sh_cmt_set_next(p, p->max_match_value);
}

static void sh_cmt_clock_event_mode(enum clock_event_mode mode,
				    struct clock_event_device *ced)
{
	struct sh_cmt_priv *p = ced_to_sh_cmt(ced);

	/* deal with old setting first */
	switch (ced->mode) {
	case CLOCK_EVT_MODE_PERIODIC:
	case CLOCK_EVT_MODE_ONESHOT:
		sh_cmt_stop(p, FLAG_CLOCKEVENT);
		break;
	default:
		break;
	}

	switch (mode) {
	case CLOCK_EVT_MODE_PERIODIC:
		dev_info(&p->pdev->dev, "used for periodic clock events\n");
		sh_cmt_clock_event_start(p, 1);
		break;
	case CLOCK_EVT_MODE_ONESHOT:
		dev_info(&p->pdev->dev, "used for oneshot clock events\n");
		sh_cmt_clock_event_start(p, 0);
		break;
	case CLOCK_EVT_MODE_SHUTDOWN:
	case CLOCK_EVT_MODE_UNUSED:
		sh_cmt_stop(p, FLAG_CLOCKEVENT);
		break;
	default:
		break;
	}
}

static int sh_cmt_clock_event_next(unsigned long delta,
				   struct clock_event_device *ced)
{
	struct sh_cmt_priv *p = ced_to_sh_cmt(ced);

	BUG_ON(ced->mode != CLOCK_EVT_MODE_ONESHOT);
	if (likely(p->flags & FLAG_IRQCONTEXT))
		p->next_match_value = delta - 1;
	else
		sh_cmt_set_next(p, delta - 1);

	return 0;
}

static void sh_cmt_register_clockevent(struct sh_cmt_priv *p,
				       char *name, unsigned long rating)
{
	struct clock_event_device *ced = &p->ced;

	memset(ced, 0, sizeof(*ced));

	ced->name = name;
	ced->features = CLOCK_EVT_FEAT_PERIODIC;
	ced->features |= CLOCK_EVT_FEAT_ONESHOT;
	ced->rating = rating;
	ced->cpumask = cpumask_of(0);
	ced->set_next_event = sh_cmt_clock_event_next;
	ced->set_mode = sh_cmt_clock_event_mode;

	dev_info(&p->pdev->dev, "used for clock events\n");
	clockevents_register_device(ced);
}

static int sh_cmt_register(struct sh_cmt_priv *p, char *name,
			   unsigned long clockevent_rating,
			   unsigned long clocksource_rating)
{
	if (p->width == (sizeof(p->max_match_value) * 8))
		p->max_match_value = ~0;
	else
		p->max_match_value = (1 << p->width) - 1;

	p->match_value = p->max_match_value;
	spin_lock_init(&p->lock);

	if (clockevent_rating)
		sh_cmt_register_clockevent(p, name, clockevent_rating);

	if (clocksource_rating)
		sh_cmt_register_clocksource(p, name, clocksource_rating);

	return 0;
}

static int sh_cmt_setup(struct sh_cmt_priv *p, struct platform_device *pdev)
{
	struct sh_timer_config *cfg = pdev->dev.platform_data;
	struct resource *res;
	int irq, ret;
	ret = -ENXIO;

	memset(p, 0, sizeof(*p));
	p->pdev = pdev;

	if (!cfg) {
		dev_err(&p->pdev->dev, "missing platform data\n");
		goto err0;
	}

	platform_set_drvdata(pdev, p);

	res = platform_get_resource(p->pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&p->pdev->dev, "failed to get I/O memory\n");
		goto err0;
	}

	irq = platform_get_irq(p->pdev, 0);
	if (irq < 0) {
		dev_err(&p->pdev->dev, "failed to get irq\n");
		goto err0;
	}

	/* map memory, let mapbase point to our channel */
	p->mapbase = ioremap_nocache(res->start, resource_size(res));
	if (p->mapbase == NULL) {
		dev_err(&p->pdev->dev, "failed to remap I/O memory\n");
		goto err0;
	}

	/* request irq using setup_irq() (too early for request_irq()) */
	p->irqaction.name = dev_name(&p->pdev->dev);
	p->irqaction.handler = sh_cmt_interrupt;
	p->irqaction.dev_id = p;
	p->irqaction.flags = IRQF_DISABLED | IRQF_TIMER | \
			     IRQF_IRQPOLL  | IRQF_NOBALANCING;

	/* get hold of clock */
	p->clk = clk_get(&p->pdev->dev, "cmt_fck");
	if (IS_ERR(p->clk)) {
		dev_err(&p->pdev->dev, "cannot get clock\n");
		ret = PTR_ERR(p->clk);
		goto err1;
	}

	if (resource_size(res) == 6) {
		p->width = 16;
		p->overflow_bit = 0x80;
		p->clear_bits = ~0x80;
	} else {
		p->width = 32;
		p->overflow_bit = 0x8000;
		p->clear_bits = ~0xc000;
	}

	ret = sh_cmt_register(p, (char *)dev_name(&p->pdev->dev),
			      cfg->clockevent_rating,
			      cfg->clocksource_rating);
	if (ret) {
		dev_err(&p->pdev->dev, "registration failed\n");
		goto err1;
	}

	ret = setup_irq(irq, &p->irqaction);
	if (ret) {
		dev_err(&p->pdev->dev, "failed to request irq %d\n", irq);
		goto err1;
	}

	return 0;

err1:
	iounmap(p->mapbase);
err0:
	return ret;
}

static int __devinit sh_cmt_probe(struct platform_device *pdev)
{
	struct sh_cmt_priv *p = platform_get_drvdata(pdev);
	int ret;

	if (p) {
		dev_info(&pdev->dev, "kept as earlytimer\n");
		return 0;
	}

	p = kmalloc(sizeof(*p), GFP_KERNEL);
	if (p == NULL) {
		dev_err(&pdev->dev, "failed to allocate driver data\n");
		return -ENOMEM;
	}

	ret = sh_cmt_setup(p, pdev);
	if (ret) {
		kfree(p);
		platform_set_drvdata(pdev, NULL);
	}
	return ret;
}

static int __devexit sh_cmt_remove(struct platform_device *pdev)
{
	return -EBUSY; /* cannot unregister clockevent and clocksource */
}

static struct platform_driver sh_cmt_device_driver = {
	.probe		= sh_cmt_probe,
	.remove		= __devexit_p(sh_cmt_remove),
	.driver		= {
		.name	= "sh_cmt",
	}
};

static int __init sh_cmt_init(void)
{
	return platform_driver_register(&sh_cmt_device_driver);
}

static void __exit sh_cmt_exit(void)
{
	platform_driver_unregister(&sh_cmt_device_driver);
}

early_platform_init("earlytimer", &sh_cmt_device_driver);
module_init(sh_cmt_init);
module_exit(sh_cmt_exit);

MODULE_AUTHOR("Magnus Damm");
MODULE_DESCRIPTION("SuperH CMT Timer Driver");
MODULE_LICENSE("GPL v2");
