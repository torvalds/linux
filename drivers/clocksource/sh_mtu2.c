/*
 * SuperH Timer Support - MTU2
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
#include <linux/clockchips.h>
#include <linux/sh_timer.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/pm_domain.h>
#include <linux/pm_runtime.h>

struct sh_mtu2_device;

struct sh_mtu2_channel {
	struct sh_mtu2_device *mtu;
	unsigned int index;

	void __iomem *base;
	int irq;

	struct clock_event_device ced;
};

struct sh_mtu2_device {
	struct platform_device *pdev;

	void __iomem *mapbase;
	struct clk *clk;

	struct sh_mtu2_channel *channels;
	unsigned int num_channels;
};

static DEFINE_RAW_SPINLOCK(sh_mtu2_lock);

#define TSTR -1 /* shared register */
#define TCR  0 /* channel register */
#define TMDR 1 /* channel register */
#define TIOR 2 /* channel register */
#define TIER 3 /* channel register */
#define TSR  4 /* channel register */
#define TCNT 5 /* channel register */
#define TGR  6 /* channel register */

static unsigned long mtu2_reg_offs[] = {
	[TCR] = 0,
	[TMDR] = 1,
	[TIOR] = 2,
	[TIER] = 4,
	[TSR] = 5,
	[TCNT] = 6,
	[TGR] = 8,
};

static inline unsigned long sh_mtu2_read(struct sh_mtu2_channel *ch, int reg_nr)
{
	unsigned long offs;

	if (reg_nr == TSTR)
		return ioread8(ch->mtu->mapbase);

	offs = mtu2_reg_offs[reg_nr];

	if ((reg_nr == TCNT) || (reg_nr == TGR))
		return ioread16(ch->base + offs);
	else
		return ioread8(ch->base + offs);
}

static inline void sh_mtu2_write(struct sh_mtu2_channel *ch, int reg_nr,
				unsigned long value)
{
	unsigned long offs;

	if (reg_nr == TSTR) {
		iowrite8(value, ch->mtu->mapbase);
		return;
	}

	offs = mtu2_reg_offs[reg_nr];

	if ((reg_nr == TCNT) || (reg_nr == TGR))
		iowrite16(value, ch->base + offs);
	else
		iowrite8(value, ch->base + offs);
}

static void sh_mtu2_start_stop_ch(struct sh_mtu2_channel *ch, int start)
{
	unsigned long flags, value;

	/* start stop register shared by multiple timer channels */
	raw_spin_lock_irqsave(&sh_mtu2_lock, flags);
	value = sh_mtu2_read(ch, TSTR);

	if (start)
		value |= 1 << ch->index;
	else
		value &= ~(1 << ch->index);

	sh_mtu2_write(ch, TSTR, value);
	raw_spin_unlock_irqrestore(&sh_mtu2_lock, flags);
}

static int sh_mtu2_enable(struct sh_mtu2_channel *ch)
{
	unsigned long periodic;
	unsigned long rate;
	int ret;

	pm_runtime_get_sync(&ch->mtu->pdev->dev);
	dev_pm_syscore_device(&ch->mtu->pdev->dev, true);

	/* enable clock */
	ret = clk_enable(ch->mtu->clk);
	if (ret) {
		dev_err(&ch->mtu->pdev->dev, "ch%u: cannot enable clock\n",
			ch->index);
		return ret;
	}

	/* make sure channel is disabled */
	sh_mtu2_start_stop_ch(ch, 0);

	rate = clk_get_rate(ch->mtu->clk) / 64;
	periodic = (rate + HZ/2) / HZ;

	/* "Periodic Counter Operation" */
	sh_mtu2_write(ch, TCR, 0x23); /* TGRA clear, divide clock by 64 */
	sh_mtu2_write(ch, TIOR, 0);
	sh_mtu2_write(ch, TGR, periodic);
	sh_mtu2_write(ch, TCNT, 0);
	sh_mtu2_write(ch, TMDR, 0);
	sh_mtu2_write(ch, TIER, 0x01);

	/* enable channel */
	sh_mtu2_start_stop_ch(ch, 1);

	return 0;
}

static void sh_mtu2_disable(struct sh_mtu2_channel *ch)
{
	/* disable channel */
	sh_mtu2_start_stop_ch(ch, 0);

	/* stop clock */
	clk_disable(ch->mtu->clk);

	dev_pm_syscore_device(&ch->mtu->pdev->dev, false);
	pm_runtime_put(&ch->mtu->pdev->dev);
}

static irqreturn_t sh_mtu2_interrupt(int irq, void *dev_id)
{
	struct sh_mtu2_channel *ch = dev_id;

	/* acknowledge interrupt */
	sh_mtu2_read(ch, TSR);
	sh_mtu2_write(ch, TSR, 0xfe);

	/* notify clockevent layer */
	ch->ced.event_handler(&ch->ced);
	return IRQ_HANDLED;
}

static struct sh_mtu2_channel *ced_to_sh_mtu2(struct clock_event_device *ced)
{
	return container_of(ced, struct sh_mtu2_channel, ced);
}

static void sh_mtu2_clock_event_mode(enum clock_event_mode mode,
				    struct clock_event_device *ced)
{
	struct sh_mtu2_channel *ch = ced_to_sh_mtu2(ced);
	int disabled = 0;

	/* deal with old setting first */
	switch (ced->mode) {
	case CLOCK_EVT_MODE_PERIODIC:
		sh_mtu2_disable(ch);
		disabled = 1;
		break;
	default:
		break;
	}

	switch (mode) {
	case CLOCK_EVT_MODE_PERIODIC:
		dev_info(&ch->mtu->pdev->dev,
			 "ch%u: used for periodic clock events\n", ch->index);
		sh_mtu2_enable(ch);
		break;
	case CLOCK_EVT_MODE_UNUSED:
		if (!disabled)
			sh_mtu2_disable(ch);
		break;
	case CLOCK_EVT_MODE_SHUTDOWN:
	default:
		break;
	}
}

static void sh_mtu2_clock_event_suspend(struct clock_event_device *ced)
{
	pm_genpd_syscore_poweroff(&ced_to_sh_mtu2(ced)->mtu->pdev->dev);
}

static void sh_mtu2_clock_event_resume(struct clock_event_device *ced)
{
	pm_genpd_syscore_poweron(&ced_to_sh_mtu2(ced)->mtu->pdev->dev);
}

static void sh_mtu2_register_clockevent(struct sh_mtu2_channel *ch,
					const char *name, unsigned long rating)
{
	struct clock_event_device *ced = &ch->ced;
	int ret;

	ced->name = name;
	ced->features = CLOCK_EVT_FEAT_PERIODIC;
	ced->rating = rating;
	ced->cpumask = cpumask_of(0);
	ced->set_mode = sh_mtu2_clock_event_mode;
	ced->suspend = sh_mtu2_clock_event_suspend;
	ced->resume = sh_mtu2_clock_event_resume;

	dev_info(&ch->mtu->pdev->dev, "ch%u: used for clock events\n",
		 ch->index);
	clockevents_register_device(ced);

	ret = request_irq(ch->irq, sh_mtu2_interrupt,
			  IRQF_TIMER | IRQF_IRQPOLL | IRQF_NOBALANCING,
			  dev_name(&ch->mtu->pdev->dev), ch);
	if (ret) {
		dev_err(&ch->mtu->pdev->dev, "ch%u: failed to request irq %d\n",
			ch->index, ch->irq);
		return;
	}
}

static int sh_mtu2_register(struct sh_mtu2_channel *ch, const char *name,
			    unsigned long clockevent_rating)
{
	if (clockevent_rating)
		sh_mtu2_register_clockevent(ch, name, clockevent_rating);

	return 0;
}

static int sh_mtu2_setup_channel(struct sh_mtu2_channel *ch,
				 struct sh_mtu2_device *mtu)
{
	struct sh_timer_config *cfg = mtu->pdev->dev.platform_data;

	ch->mtu = mtu;
	ch->index = cfg->timer_bit;

	ch->irq = platform_get_irq(mtu->pdev, 0);
	if (ch->irq < 0) {
		dev_err(&mtu->pdev->dev, "ch%u: failed to get irq\n",
			ch->index);
		return ch->irq;
	}

	return sh_mtu2_register(ch, dev_name(&mtu->pdev->dev),
				cfg->clockevent_rating);
}

static int sh_mtu2_setup(struct sh_mtu2_device *mtu,
			 struct platform_device *pdev)
{
	struct sh_timer_config *cfg = pdev->dev.platform_data;
	struct resource *res;
	void __iomem *base;
	int ret;
	ret = -ENXIO;

	mtu->pdev = pdev;

	if (!cfg) {
		dev_err(&mtu->pdev->dev, "missing platform data\n");
		goto err0;
	}

	platform_set_drvdata(pdev, mtu);

	res = platform_get_resource(mtu->pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&mtu->pdev->dev, "failed to get I/O memory\n");
		goto err0;
	}

	/*
	 * Map memory, let base point to our channel and mapbase to the
	 * start/stop shared register.
	 */
	base = ioremap_nocache(res->start, resource_size(res));
	if (base == NULL) {
		dev_err(&mtu->pdev->dev, "failed to remap I/O memory\n");
		goto err0;
	}

	mtu->mapbase = base + cfg->channel_offset;

	/* get hold of clock */
	mtu->clk = clk_get(&mtu->pdev->dev, "mtu2_fck");
	if (IS_ERR(mtu->clk)) {
		dev_err(&mtu->pdev->dev, "cannot get clock\n");
		ret = PTR_ERR(mtu->clk);
		goto err1;
	}

	ret = clk_prepare(mtu->clk);
	if (ret < 0)
		goto err2;

	mtu->channels = kzalloc(sizeof(*mtu->channels), GFP_KERNEL);
	if (mtu->channels == NULL) {
		ret = -ENOMEM;
		goto err3;
	}

	mtu->num_channels = 1;

	mtu->channels[0].base = base;

	ret = sh_mtu2_setup_channel(&mtu->channels[0], mtu);
	if (ret < 0)
		goto err3;

	return 0;
 err3:
	kfree(mtu->channels);
	clk_unprepare(mtu->clk);
 err2:
	clk_put(mtu->clk);
 err1:
	iounmap(base);
 err0:
	return ret;
}

static int sh_mtu2_probe(struct platform_device *pdev)
{
	struct sh_mtu2_device *mtu = platform_get_drvdata(pdev);
	struct sh_timer_config *cfg = pdev->dev.platform_data;
	int ret;

	if (!is_early_platform_device(pdev)) {
		pm_runtime_set_active(&pdev->dev);
		pm_runtime_enable(&pdev->dev);
	}

	if (mtu) {
		dev_info(&pdev->dev, "kept as earlytimer\n");
		goto out;
	}

	mtu = kzalloc(sizeof(*mtu), GFP_KERNEL);
	if (mtu == NULL) {
		dev_err(&pdev->dev, "failed to allocate driver data\n");
		return -ENOMEM;
	}

	ret = sh_mtu2_setup(mtu, pdev);
	if (ret) {
		kfree(mtu);
		pm_runtime_idle(&pdev->dev);
		return ret;
	}
	if (is_early_platform_device(pdev))
		return 0;

 out:
	if (cfg->clockevent_rating)
		pm_runtime_irq_safe(&pdev->dev);
	else
		pm_runtime_idle(&pdev->dev);

	return 0;
}

static int sh_mtu2_remove(struct platform_device *pdev)
{
	return -EBUSY; /* cannot unregister clockevent */
}

static struct platform_driver sh_mtu2_device_driver = {
	.probe		= sh_mtu2_probe,
	.remove		= sh_mtu2_remove,
	.driver		= {
		.name	= "sh_mtu2",
	}
};

static int __init sh_mtu2_init(void)
{
	return platform_driver_register(&sh_mtu2_device_driver);
}

static void __exit sh_mtu2_exit(void)
{
	platform_driver_unregister(&sh_mtu2_device_driver);
}

early_platform_init("earlytimer", &sh_mtu2_device_driver);
subsys_initcall(sh_mtu2_init);
module_exit(sh_mtu2_exit);

MODULE_AUTHOR("Magnus Damm");
MODULE_DESCRIPTION("SuperH MTU2 Timer Driver");
MODULE_LICENSE("GPL v2");
