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
#include <linux/pm_domain.h>
#include <linux/pm_runtime.h>

struct sh_tmu_device;

struct sh_tmu_channel {
	struct sh_tmu_device *tmu;

	int irq;

	unsigned long rate;
	unsigned long periodic;
	struct clock_event_device ced;
	struct clocksource cs;
	bool cs_enabled;
	unsigned int enable_count;
};

struct sh_tmu_device {
	struct platform_device *pdev;

	void __iomem *mapbase;
	struct clk *clk;

	struct sh_tmu_channel channel;
};

static DEFINE_RAW_SPINLOCK(sh_tmu_lock);

#define TSTR -1 /* shared register */
#define TCOR  0 /* channel register */
#define TCNT 1 /* channel register */
#define TCR 2 /* channel register */

static inline unsigned long sh_tmu_read(struct sh_tmu_channel *ch, int reg_nr)
{
	struct sh_timer_config *cfg = ch->tmu->pdev->dev.platform_data;
	void __iomem *base = ch->tmu->mapbase;
	unsigned long offs;

	if (reg_nr == TSTR)
		return ioread8(base - cfg->channel_offset);

	offs = reg_nr << 2;

	if (reg_nr == TCR)
		return ioread16(base + offs);
	else
		return ioread32(base + offs);
}

static inline void sh_tmu_write(struct sh_tmu_channel *ch, int reg_nr,
				unsigned long value)
{
	struct sh_timer_config *cfg = ch->tmu->pdev->dev.platform_data;
	void __iomem *base = ch->tmu->mapbase;
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

static void sh_tmu_start_stop_ch(struct sh_tmu_channel *ch, int start)
{
	struct sh_timer_config *cfg = ch->tmu->pdev->dev.platform_data;
	unsigned long flags, value;

	/* start stop register shared by multiple timer channels */
	raw_spin_lock_irqsave(&sh_tmu_lock, flags);
	value = sh_tmu_read(ch, TSTR);

	if (start)
		value |= 1 << cfg->timer_bit;
	else
		value &= ~(1 << cfg->timer_bit);

	sh_tmu_write(ch, TSTR, value);
	raw_spin_unlock_irqrestore(&sh_tmu_lock, flags);
}

static int __sh_tmu_enable(struct sh_tmu_channel *ch)
{
	int ret;

	/* enable clock */
	ret = clk_enable(ch->tmu->clk);
	if (ret) {
		dev_err(&ch->tmu->pdev->dev, "cannot enable clock\n");
		return ret;
	}

	/* make sure channel is disabled */
	sh_tmu_start_stop_ch(ch, 0);

	/* maximum timeout */
	sh_tmu_write(ch, TCOR, 0xffffffff);
	sh_tmu_write(ch, TCNT, 0xffffffff);

	/* configure channel to parent clock / 4, irq off */
	ch->rate = clk_get_rate(ch->tmu->clk) / 4;
	sh_tmu_write(ch, TCR, 0x0000);

	/* enable channel */
	sh_tmu_start_stop_ch(ch, 1);

	return 0;
}

static int sh_tmu_enable(struct sh_tmu_channel *ch)
{
	if (ch->enable_count++ > 0)
		return 0;

	pm_runtime_get_sync(&ch->tmu->pdev->dev);
	dev_pm_syscore_device(&ch->tmu->pdev->dev, true);

	return __sh_tmu_enable(ch);
}

static void __sh_tmu_disable(struct sh_tmu_channel *ch)
{
	/* disable channel */
	sh_tmu_start_stop_ch(ch, 0);

	/* disable interrupts in TMU block */
	sh_tmu_write(ch, TCR, 0x0000);

	/* stop clock */
	clk_disable(ch->tmu->clk);
}

static void sh_tmu_disable(struct sh_tmu_channel *ch)
{
	if (WARN_ON(ch->enable_count == 0))
		return;

	if (--ch->enable_count > 0)
		return;

	__sh_tmu_disable(ch);

	dev_pm_syscore_device(&ch->tmu->pdev->dev, false);
	pm_runtime_put(&ch->tmu->pdev->dev);
}

static void sh_tmu_set_next(struct sh_tmu_channel *ch, unsigned long delta,
			    int periodic)
{
	/* stop timer */
	sh_tmu_start_stop_ch(ch, 0);

	/* acknowledge interrupt */
	sh_tmu_read(ch, TCR);

	/* enable interrupt */
	sh_tmu_write(ch, TCR, 0x0020);

	/* reload delta value in case of periodic timer */
	if (periodic)
		sh_tmu_write(ch, TCOR, delta);
	else
		sh_tmu_write(ch, TCOR, 0xffffffff);

	sh_tmu_write(ch, TCNT, delta);

	/* start timer */
	sh_tmu_start_stop_ch(ch, 1);
}

static irqreturn_t sh_tmu_interrupt(int irq, void *dev_id)
{
	struct sh_tmu_channel *ch = dev_id;

	/* disable or acknowledge interrupt */
	if (ch->ced.mode == CLOCK_EVT_MODE_ONESHOT)
		sh_tmu_write(ch, TCR, 0x0000);
	else
		sh_tmu_write(ch, TCR, 0x0020);

	/* notify clockevent layer */
	ch->ced.event_handler(&ch->ced);
	return IRQ_HANDLED;
}

static struct sh_tmu_channel *cs_to_sh_tmu(struct clocksource *cs)
{
	return container_of(cs, struct sh_tmu_channel, cs);
}

static cycle_t sh_tmu_clocksource_read(struct clocksource *cs)
{
	struct sh_tmu_channel *ch = cs_to_sh_tmu(cs);

	return sh_tmu_read(ch, TCNT) ^ 0xffffffff;
}

static int sh_tmu_clocksource_enable(struct clocksource *cs)
{
	struct sh_tmu_channel *ch = cs_to_sh_tmu(cs);
	int ret;

	if (WARN_ON(ch->cs_enabled))
		return 0;

	ret = sh_tmu_enable(ch);
	if (!ret) {
		__clocksource_updatefreq_hz(cs, ch->rate);
		ch->cs_enabled = true;
	}

	return ret;
}

static void sh_tmu_clocksource_disable(struct clocksource *cs)
{
	struct sh_tmu_channel *ch = cs_to_sh_tmu(cs);

	if (WARN_ON(!ch->cs_enabled))
		return;

	sh_tmu_disable(ch);
	ch->cs_enabled = false;
}

static void sh_tmu_clocksource_suspend(struct clocksource *cs)
{
	struct sh_tmu_channel *ch = cs_to_sh_tmu(cs);

	if (!ch->cs_enabled)
		return;

	if (--ch->enable_count == 0) {
		__sh_tmu_disable(ch);
		pm_genpd_syscore_poweroff(&ch->tmu->pdev->dev);
	}
}

static void sh_tmu_clocksource_resume(struct clocksource *cs)
{
	struct sh_tmu_channel *ch = cs_to_sh_tmu(cs);

	if (!ch->cs_enabled)
		return;

	if (ch->enable_count++ == 0) {
		pm_genpd_syscore_poweron(&ch->tmu->pdev->dev);
		__sh_tmu_enable(ch);
	}
}

static int sh_tmu_register_clocksource(struct sh_tmu_channel *ch,
				       char *name, unsigned long rating)
{
	struct clocksource *cs = &ch->cs;

	cs->name = name;
	cs->rating = rating;
	cs->read = sh_tmu_clocksource_read;
	cs->enable = sh_tmu_clocksource_enable;
	cs->disable = sh_tmu_clocksource_disable;
	cs->suspend = sh_tmu_clocksource_suspend;
	cs->resume = sh_tmu_clocksource_resume;
	cs->mask = CLOCKSOURCE_MASK(32);
	cs->flags = CLOCK_SOURCE_IS_CONTINUOUS;

	dev_info(&ch->tmu->pdev->dev, "used as clock source\n");

	/* Register with dummy 1 Hz value, gets updated in ->enable() */
	clocksource_register_hz(cs, 1);
	return 0;
}

static struct sh_tmu_channel *ced_to_sh_tmu(struct clock_event_device *ced)
{
	return container_of(ced, struct sh_tmu_channel, ced);
}

static void sh_tmu_clock_event_start(struct sh_tmu_channel *ch, int periodic)
{
	struct clock_event_device *ced = &ch->ced;

	sh_tmu_enable(ch);

	clockevents_config(ced, ch->rate);

	if (periodic) {
		ch->periodic = (ch->rate + HZ/2) / HZ;
		sh_tmu_set_next(ch, ch->periodic, 1);
	}
}

static void sh_tmu_clock_event_mode(enum clock_event_mode mode,
				    struct clock_event_device *ced)
{
	struct sh_tmu_channel *ch = ced_to_sh_tmu(ced);
	int disabled = 0;

	/* deal with old setting first */
	switch (ced->mode) {
	case CLOCK_EVT_MODE_PERIODIC:
	case CLOCK_EVT_MODE_ONESHOT:
		sh_tmu_disable(ch);
		disabled = 1;
		break;
	default:
		break;
	}

	switch (mode) {
	case CLOCK_EVT_MODE_PERIODIC:
		dev_info(&ch->tmu->pdev->dev,
			 "used for periodic clock events\n");
		sh_tmu_clock_event_start(ch, 1);
		break;
	case CLOCK_EVT_MODE_ONESHOT:
		dev_info(&ch->tmu->pdev->dev,
			 "used for oneshot clock events\n");
		sh_tmu_clock_event_start(ch, 0);
		break;
	case CLOCK_EVT_MODE_UNUSED:
		if (!disabled)
			sh_tmu_disable(ch);
		break;
	case CLOCK_EVT_MODE_SHUTDOWN:
	default:
		break;
	}
}

static int sh_tmu_clock_event_next(unsigned long delta,
				   struct clock_event_device *ced)
{
	struct sh_tmu_channel *ch = ced_to_sh_tmu(ced);

	BUG_ON(ced->mode != CLOCK_EVT_MODE_ONESHOT);

	/* program new delta value */
	sh_tmu_set_next(ch, delta, 0);
	return 0;
}

static void sh_tmu_clock_event_suspend(struct clock_event_device *ced)
{
	pm_genpd_syscore_poweroff(&ced_to_sh_tmu(ced)->tmu->pdev->dev);
}

static void sh_tmu_clock_event_resume(struct clock_event_device *ced)
{
	pm_genpd_syscore_poweron(&ced_to_sh_tmu(ced)->tmu->pdev->dev);
}

static void sh_tmu_register_clockevent(struct sh_tmu_channel *ch,
				       char *name, unsigned long rating)
{
	struct clock_event_device *ced = &ch->ced;
	int ret;

	memset(ced, 0, sizeof(*ced));

	ced->name = name;
	ced->features = CLOCK_EVT_FEAT_PERIODIC;
	ced->features |= CLOCK_EVT_FEAT_ONESHOT;
	ced->rating = rating;
	ced->cpumask = cpumask_of(0);
	ced->set_next_event = sh_tmu_clock_event_next;
	ced->set_mode = sh_tmu_clock_event_mode;
	ced->suspend = sh_tmu_clock_event_suspend;
	ced->resume = sh_tmu_clock_event_resume;

	dev_info(&ch->tmu->pdev->dev, "used for clock events\n");

	clockevents_config_and_register(ced, 1, 0x300, 0xffffffff);

	ret = request_irq(ch->irq, sh_tmu_interrupt,
			  IRQF_TIMER | IRQF_IRQPOLL | IRQF_NOBALANCING,
			  dev_name(&ch->tmu->pdev->dev), ch);
	if (ret) {
		dev_err(&ch->tmu->pdev->dev, "failed to request irq %d\n",
			ch->irq);
		return;
	}
}

static int sh_tmu_register(struct sh_tmu_channel *ch, char *name,
		    unsigned long clockevent_rating,
		    unsigned long clocksource_rating)
{
	if (clockevent_rating)
		sh_tmu_register_clockevent(ch, name, clockevent_rating);
	else if (clocksource_rating)
		sh_tmu_register_clocksource(ch, name, clocksource_rating);

	return 0;
}

static int sh_tmu_setup(struct sh_tmu_device *tmu, struct platform_device *pdev)
{
	struct sh_timer_config *cfg = pdev->dev.platform_data;
	struct resource *res;
	int ret;
	ret = -ENXIO;

	memset(tmu, 0, sizeof(*tmu));
	tmu->pdev = pdev;

	if (!cfg) {
		dev_err(&tmu->pdev->dev, "missing platform data\n");
		goto err0;
	}

	platform_set_drvdata(pdev, tmu);

	res = platform_get_resource(tmu->pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&tmu->pdev->dev, "failed to get I/O memory\n");
		goto err0;
	}

	tmu->channel.irq = platform_get_irq(tmu->pdev, 0);
	if (tmu->channel.irq < 0) {
		dev_err(&tmu->pdev->dev, "failed to get irq\n");
		goto err0;
	}

	/* map memory, let mapbase point to our channel */
	tmu->mapbase = ioremap_nocache(res->start, resource_size(res));
	if (tmu->mapbase == NULL) {
		dev_err(&tmu->pdev->dev, "failed to remap I/O memory\n");
		goto err0;
	}

	/* get hold of clock */
	tmu->clk = clk_get(&tmu->pdev->dev, "tmu_fck");
	if (IS_ERR(tmu->clk)) {
		dev_err(&tmu->pdev->dev, "cannot get clock\n");
		ret = PTR_ERR(tmu->clk);
		goto err1;
	}

	ret = clk_prepare(tmu->clk);
	if (ret < 0)
		goto err2;

	tmu->channel.cs_enabled = false;
	tmu->channel.enable_count = 0;
	tmu->channel.tmu = tmu;

	ret = sh_tmu_register(&tmu->channel, (char *)dev_name(&tmu->pdev->dev),
			      cfg->clockevent_rating,
			      cfg->clocksource_rating);
	if (ret < 0)
		goto err3;

	return 0;

 err3:
	clk_unprepare(tmu->clk);
 err2:
	clk_put(tmu->clk);
 err1:
	iounmap(tmu->mapbase);
 err0:
	return ret;
}

static int sh_tmu_probe(struct platform_device *pdev)
{
	struct sh_tmu_device *tmu = platform_get_drvdata(pdev);
	struct sh_timer_config *cfg = pdev->dev.platform_data;
	int ret;

	if (!is_early_platform_device(pdev)) {
		pm_runtime_set_active(&pdev->dev);
		pm_runtime_enable(&pdev->dev);
	}

	if (tmu) {
		dev_info(&pdev->dev, "kept as earlytimer\n");
		goto out;
	}

	tmu = kmalloc(sizeof(*tmu), GFP_KERNEL);
	if (tmu == NULL) {
		dev_err(&pdev->dev, "failed to allocate driver data\n");
		return -ENOMEM;
	}

	ret = sh_tmu_setup(tmu, pdev);
	if (ret) {
		kfree(tmu);
		pm_runtime_idle(&pdev->dev);
		return ret;
	}
	if (is_early_platform_device(pdev))
		return 0;

 out:
	if (cfg->clockevent_rating || cfg->clocksource_rating)
		pm_runtime_irq_safe(&pdev->dev);
	else
		pm_runtime_idle(&pdev->dev);

	return 0;
}

static int sh_tmu_remove(struct platform_device *pdev)
{
	return -EBUSY; /* cannot unregister clockevent and clocksource */
}

static struct platform_driver sh_tmu_device_driver = {
	.probe		= sh_tmu_probe,
	.remove		= sh_tmu_remove,
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
subsys_initcall(sh_tmu_init);
module_exit(sh_tmu_exit);

MODULE_AUTHOR("Magnus Damm");
MODULE_DESCRIPTION("SuperH TMU Timer Driver");
MODULE_LICENSE("GPL v2");
