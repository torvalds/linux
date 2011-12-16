/*
 * SuperH Timer Support - TMU
 *
 *  Copyright (C) 2009 Magnus Damm
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
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/clk.h>
#include <linux/irq.h>
#include <linux/err.h>
#include <linux/clocksource.h>
#include <linux/clockchips.h>
#include <linux/sh_timer.h>
#include <linux/slab.h>
#include <linux/module.h>

struct sh_tmu_priv {
	void __iomem *mapbase;
	struct clk *clk;
	struct irqaction irqaction;
	struct platform_device *pdev;
	unsigned long rate;
	unsigned long periodic;
	struct clock_event_device ced;
	struct clocksource cs;
};

static DEFINE_SPINLOCK(sh_tmu_lock);

#define TSTR -1 /* shared register */
#define TCOR  0 /* channel register */
#define TCNT 1 /* channel register */
#define TCR 2 /* channel register */

static inline unsigned long sh_tmu_read(struct sh_tmu_priv *p, int reg_nr)
{
	struct sh_timer_config *cfg = p->pdev->dev.platform_data;
	void __iomem *base = p->mapbase;
	unsigned long offs;

	if (reg_nr == TSTR)
		return ioread8(base - cfg->channel_offset);

	offs = reg_nr << 2;

	if (reg_nr == TCR)
		return ioread16(base + offs);
	else
		return ioread32(base + offs);
}

static inline void sh_tmu_write(struct sh_tmu_priv *p, int reg_nr,
				unsigned long value)
{
	struct sh_timer_config *cfg = p->pdev->dev.platform_data;
	void __iomem *base = p->mapbase;
	unsigned long offs;

	if (reg_nr == TSTR) {
		iowrite8(value, base - cfg->channel_offset);
		return;
	}

	offs = reg_nr << 2;

	if (reg_nr == TCR)
		iowrite16(value, base + offs);
	else
		iowrite32(value, base + offs);
}

static void sh_tmu_start_stop_ch(struct sh_tmu_priv *p, int start)
{
	struct sh_timer_config *cfg = p->pdev->dev.platform_data;
	unsigned long flags, value;

	/* start stop register shared by multiple timer channels */
	spin_lock_irqsave(&sh_tmu_lock, flags);
	value = sh_tmu_read(p, TSTR);

	if (start)
		value |= 1 << cfg->timer_bit;
	else
		value &= ~(1 << cfg->timer_bit);

	sh_tmu_write(p, TSTR, value);
	spin_unlock_irqrestore(&sh_tmu_lock, flags);
}

static int sh_tmu_enable(struct sh_tmu_priv *p)
{
	int ret;

	/* enable clock */
	ret = clk_enable(p->clk);
	if (ret) {
		dev_err(&p->pdev->dev, "cannot enable clock\n");
		return ret;
	}

	/* make sure channel is disabled */
	sh_tmu_start_stop_ch(p, 0);

	/* maximum timeout */
	sh_tmu_write(p, TCOR, 0xffffffff);
	sh_tmu_write(p, TCNT, 0xffffffff);

	/* configure channel to parent clock / 4, irq off */
	p->rate = clk_get_rate(p->clk) / 4;
	sh_tmu_write(p, TCR, 0x0000);

	/* enable channel */
	sh_tmu_start_stop_ch(p, 1);

	return 0;
}

static void sh_tmu_disable(struct sh_tmu_priv *p)
{
	/* disable channel */
	sh_tmu_start_stop_ch(p, 0);

	/* disable interrupts in TMU block */
	sh_tmu_write(p, TCR, 0x0000);

	/* stop clock */
	clk_disable(p->clk);
}

static void sh_tmu_set_next(struct sh_tmu_priv *p, unsigned long delta,
			    int periodic)
{
	/* stop timer */
	sh_tmu_start_stop_ch(p, 0);

	/* acknowledge interrupt */
	sh_tmu_read(p, TCR);

	/* enable interrupt */
	sh_tmu_write(p, TCR, 0x0020);

	/* reload delta value in case of periodic timer */
	if (periodic)
		sh_tmu_write(p, TCOR, delta);
	else
		sh_tmu_write(p, TCOR, 0xffffffff);

	sh_tmu_write(p, TCNT, delta);

	/* start timer */
	sh_tmu_start_stop_ch(p, 1);
}

static irqreturn_t sh_tmu_interrupt(int irq, void *dev_id)
{
	struct sh_tmu_priv *p = dev_id;

	/* disable or acknowledge interrupt */
	if (p->ced.mode == CLOCK_EVT_MODE_ONESHOT)
		sh_tmu_write(p, TCR, 0x0000);
	else
		sh_tmu_write(p, TCR, 0x0020);

	/* notify clockevent layer */
	p->ced.event_handler(&p->ced);
	return IRQ_HANDLED;
}

static struct sh_tmu_priv *cs_to_sh_tmu(struct clocksource *cs)
{
	return container_of(cs, struct sh_tmu_priv, cs);
}

static cycle_t sh_tmu_clocksource_read(struct clocksource *cs)
{
	struct sh_tmu_priv *p = cs_to_sh_tmu(cs);

	return sh_tmu_read(p, TCNT) ^ 0xffffffff;
}

static int sh_tmu_clocksource_enable(struct clocksource *cs)
{
	struct sh_tmu_priv *p = cs_to_sh_tmu(cs);
	int ret;

	ret = sh_tmu_enable(p);
	if (!ret)
		__clocksource_updatefreq_hz(cs, p->rate);
	return ret;
}

static void sh_tmu_clocksource_disable(struct clocksource *cs)
{
	sh_tmu_disable(cs_to_sh_tmu(cs));
}

static int sh_tmu_register_clocksource(struct sh_tmu_priv *p,
				       char *name, unsigned long rating)
{
	struct clocksource *cs = &p->cs;

	cs->name = name;
	cs->rating = rating;
	cs->read = sh_tmu_clocksource_read;
	cs->enable = sh_tmu_clocksource_enable;
	cs->disable = sh_tmu_clocksource_disable;
	cs->mask = CLOCKSOURCE_MASK(32);
	cs->flags = CLOCK_SOURCE_IS_CONTINUOUS;

	dev_info(&p->pdev->dev, "used as clock source\n");

	/* Register with dummy 1 Hz value, gets updated in ->enable() */
	clocksource_register_hz(cs, 1);
	return 0;
}

static struct sh_tmu_priv *ced_to_sh_tmu(struct clock_event_device *ced)
{
	return container_of(ced, struct sh_tmu_priv, ced);
}

static void sh_tmu_clock_event_start(struct sh_tmu_priv *p, int periodic)
{
	struct clock_event_device *ced = &p->ced;

	sh_tmu_enable(p);

	/* TODO: calculate good shift from rate and counter bit width */

	ced->shift = 32;
	ced->mult = div_sc(p->rate, NSEC_PER_SEC, ced->shift);
	ced->max_delta_ns = clockevent_delta2ns(0xffffffff, ced);
	ced->min_delta_ns = 5000;

	if (periodic) {
		p->periodic = (p->rate + HZ/2) / HZ;
		sh_tmu_set_next(p, p->periodic, 1);
	}
}

static void sh_tmu_clock_event_mode(enum clock_event_mode mode,
				    struct clock_event_device *ced)
{
	struct sh_tmu_priv *p = ced_to_sh_tmu(ced);
	int disabled = 0;

	/* deal with old setting first */
	switch (ced->mode) {
	case CLOCK_EVT_MODE_PERIODIC:
	case CLOCK_EVT_MODE_ONESHOT:
		sh_tmu_disable(p);
		disabled = 1;
		break;
	default:
		break;
	}

	switch (mode) {
	case CLOCK_EVT_MODE_PERIODIC:
		dev_info(&p->pdev->dev, "used for periodic clock events\n");
		sh_tmu_clock_event_start(p, 1);
		break;
	case CLOCK_EVT_MODE_ONESHOT:
		dev_info(&p->pdev->dev, "used for oneshot clock events\n");
		sh_tmu_clock_event_start(p, 0);
		break;
	case CLOCK_EVT_MODE_UNUSED:
		if (!disabled)
			sh_tmu_disable(p);
		break;
	case CLOCK_EVT_MODE_SHUTDOWN:
	default:
		break;
	}
}

static int sh_tmu_clock_event_next(unsigned long delta,
				   struct clock_event_device *ced)
{
	struct sh_tmu_priv *p = ced_to_sh_tmu(ced);

	BUG_ON(ced->mode != CLOCK_EVT_MODE_ONESHOT);

	/* program new delta value */
	sh_tmu_set_next(p, delta, 0);
	return 0;
}

static void sh_tmu_register_clockevent(struct sh_tmu_priv *p,
				       char *name, unsigned long rating)
{
	struct clock_event_device *ced = &p->ced;
	int ret;

	memset(ced, 0, sizeof(*ced));

	ced->name = name;
	ced->features = CLOCK_EVT_FEAT_PERIODIC;
	ced->features |= CLOCK_EVT_FEAT_ONESHOT;
	ced->rating = rating;
	ced->cpumask = cpumask_of(0);
	ced->set_next_event = sh_tmu_clock_event_next;
	ced->set_mode = sh_tmu_clock_event_mode;

	dev_info(&p->pdev->dev, "used for clock events\n");
	clockevents_register_device(ced);

	ret = setup_irq(p->irqaction.irq, &p->irqaction);
	if (ret) {
		dev_err(&p->pdev->dev, "failed to request irq %d\n",
			p->irqaction.irq);
		return;
	}
}

static int sh_tmu_register(struct sh_tmu_priv *p, char *name,
		    unsigned long clockevent_rating,
		    unsigned long clocksource_rating)
{
	if (clockevent_rating)
		sh_tmu_register_clockevent(p, name, clockevent_rating);
	else if (clocksource_rating)
		sh_tmu_register_clocksource(p, name, clocksource_rating);

	return 0;
}

static int sh_tmu_setup(struct sh_tmu_priv *p, struct platform_device *pdev)
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

	/* setup data for setup_irq() (too early for request_irq()) */
	p->irqaction.name = dev_name(&p->pdev->dev);
	p->irqaction.handler = sh_tmu_interrupt;
	p->irqaction.dev_id = p;
	p->irqaction.irq = irq;
	p->irqaction.flags = IRQF_DISABLED | IRQF_TIMER | \
			     IRQF_IRQPOLL  | IRQF_NOBALANCING;

	/* get hold of clock */
	p->clk = clk_get(&p->pdev->dev, "tmu_fck");
	if (IS_ERR(p->clk)) {
		dev_err(&p->pdev->dev, "cannot get clock\n");
		ret = PTR_ERR(p->clk);
		goto err1;
	}

	return sh_tmu_register(p, (char *)dev_name(&p->pdev->dev),
			       cfg->clockevent_rating,
			       cfg->clocksource_rating);
 err1:
	iounmap(p->mapbase);
 err0:
	return ret;
}

static int __devinit sh_tmu_probe(struct platform_device *pdev)
{
	struct sh_tmu_priv *p = platform_get_drvdata(pdev);
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

	ret = sh_tmu_setup(p, pdev);
	if (ret) {
		kfree(p);
		platform_set_drvdata(pdev, NULL);
	}
	return ret;
}

static int __devexit sh_tmu_remove(struct platform_device *pdev)
{
	return -EBUSY; /* cannot unregister clockevent and clocksource */
}

static struct platform_driver sh_tmu_device_driver = {
	.probe		= sh_tmu_probe,
	.remove		= __devexit_p(sh_tmu_remove),
	.driver		= {
		.name	= "sh_tmu",
	}
};

static int __init sh_tmu_init(void)
{
	return platform_driver_register(&sh_tmu_device_driver);
}

static void __exit sh_tmu_exit(void)
{
	platform_driver_unregister(&sh_tmu_device_driver);
}

early_platform_init("earlytimer", &sh_tmu_device_driver);
module_init(sh_tmu_init);
module_exit(sh_tmu_exit);

MODULE_AUTHOR("Magnus Damm");
MODULE_DESCRIPTION("SuperH TMU Timer Driver");
MODULE_LICENSE("GPL v2");
