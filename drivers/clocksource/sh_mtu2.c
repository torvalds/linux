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

struct sh_mtu2_priv {
	void __iomem *mapbase;
	struct clk *clk;
	struct irqaction irqaction;
	struct platform_device *pdev;
	unsigned long rate;
	unsigned long periodic;
	struct clock_event_device ced;
};

static DEFINE_SPINLOCK(sh_mtu2_lock);

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

static inline unsigned long sh_mtu2_read(struct sh_mtu2_priv *p, int reg_nr)
{
	struct sh_timer_config *cfg = p->pdev->dev.platform_data;
	void __iomem *base = p->mapbase;
	unsigned long offs;

	if (reg_nr == TSTR)
		return ioread8(base + cfg->channel_offset);

	offs = mtu2_reg_offs[reg_nr];

	if ((reg_nr == TCNT) || (reg_nr == TGR))
		return ioread16(base + offs);
	else
		return ioread8(base + offs);
}

static inline void sh_mtu2_write(struct sh_mtu2_priv *p, int reg_nr,
				unsigned long value)
{
	struct sh_timer_config *cfg = p->pdev->dev.platform_data;
	void __iomem *base = p->mapbase;
	unsigned long offs;

	if (reg_nr == TSTR) {
		iowrite8(value, base + cfg->channel_offset);
		return;
	}

	offs = mtu2_reg_offs[reg_nr];

	if ((reg_nr == TCNT) || (reg_nr == TGR))
		iowrite16(value, base + offs);
	else
		iowrite8(value, base + offs);
}

static void sh_mtu2_start_stop_ch(struct sh_mtu2_priv *p, int start)
{
	struct sh_timer_config *cfg = p->pdev->dev.platform_data;
	unsigned long flags, value;

	/* start stop register shared by multiple timer channels */
	spin_lock_irqsave(&sh_mtu2_lock, flags);
	value = sh_mtu2_read(p, TSTR);

	if (start)
		value |= 1 << cfg->timer_bit;
	else
		value &= ~(1 << cfg->timer_bit);

	sh_mtu2_write(p, TSTR, value);
	spin_unlock_irqrestore(&sh_mtu2_lock, flags);
}

static int sh_mtu2_enable(struct sh_mtu2_priv *p)
{
	struct sh_timer_config *cfg = p->pdev->dev.platform_data;
	int ret;

	/* enable clock */
	ret = clk_enable(p->clk);
	if (ret) {
		pr_err("sh_mtu2: cannot enable clock \"%s\"\n", cfg->clk);
		return ret;
	}

	/* make sure channel is disabled */
	sh_mtu2_start_stop_ch(p, 0);

	p->rate = clk_get_rate(p->clk) / 64;
	p->periodic = (p->rate + HZ/2) / HZ;

	/* "Periodic Counter Operation" */
	sh_mtu2_write(p, TCR, 0x23); /* TGRA clear, divide clock by 64 */
	sh_mtu2_write(p, TIOR, 0);
	sh_mtu2_write(p, TGR, p->periodic);
	sh_mtu2_write(p, TCNT, 0);
	sh_mtu2_write(p, TMDR, 0);
	sh_mtu2_write(p, TIER, 0x01);

	/* enable channel */
	sh_mtu2_start_stop_ch(p, 1);

	return 0;
}

static void sh_mtu2_disable(struct sh_mtu2_priv *p)
{
	/* disable channel */
	sh_mtu2_start_stop_ch(p, 0);

	/* stop clock */
	clk_disable(p->clk);
}

static irqreturn_t sh_mtu2_interrupt(int irq, void *dev_id)
{
	struct sh_mtu2_priv *p = dev_id;

	/* acknowledge interrupt */
	sh_mtu2_read(p, TSR);
	sh_mtu2_write(p, TSR, 0xfe);

	/* notify clockevent layer */
	p->ced.event_handler(&p->ced);
	return IRQ_HANDLED;
}

static struct sh_mtu2_priv *ced_to_sh_mtu2(struct clock_event_device *ced)
{
	return container_of(ced, struct sh_mtu2_priv, ced);
}

static void sh_mtu2_clock_event_mode(enum clock_event_mode mode,
				    struct clock_event_device *ced)
{
	struct sh_mtu2_priv *p = ced_to_sh_mtu2(ced);
	int disabled = 0;

	/* deal with old setting first */
	switch (ced->mode) {
	case CLOCK_EVT_MODE_PERIODIC:
		sh_mtu2_disable(p);
		disabled = 1;
		break;
	default:
		break;
	}

	switch (mode) {
	case CLOCK_EVT_MODE_PERIODIC:
		pr_info("sh_mtu2: %s used for periodic clock events\n",
			ced->name);
		sh_mtu2_enable(p);
		break;
	case CLOCK_EVT_MODE_UNUSED:
		if (!disabled)
			sh_mtu2_disable(p);
		break;
	case CLOCK_EVT_MODE_SHUTDOWN:
	default:
		break;
	}
}

static void sh_mtu2_register_clockevent(struct sh_mtu2_priv *p,
				       char *name, unsigned long rating)
{
	struct clock_event_device *ced = &p->ced;
	int ret;

	memset(ced, 0, sizeof(*ced));

	ced->name = name;
	ced->features = CLOCK_EVT_FEAT_PERIODIC;
	ced->rating = rating;
	ced->cpumask = cpumask_of(0);
	ced->set_mode = sh_mtu2_clock_event_mode;

	pr_info("sh_mtu2: %s used for clock events\n", ced->name);
	clockevents_register_device(ced);

	ret = setup_irq(p->irqaction.irq, &p->irqaction);
	if (ret) {
		pr_err("sh_mtu2: failed to request irq %d\n",
		       p->irqaction.irq);
		return;
	}
}

static int sh_mtu2_register(struct sh_mtu2_priv *p, char *name,
			    unsigned long clockevent_rating)
{
	if (clockevent_rating)
		sh_mtu2_register_clockevent(p, name, clockevent_rating);

	return 0;
}

static int sh_mtu2_setup(struct sh_mtu2_priv *p, struct platform_device *pdev)
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
		pr_err("sh_mtu2: failed to remap I/O memory\n");
		goto err0;
	}

	/* setup data for setup_irq() (too early for request_irq()) */
	p->irqaction.name = cfg->name;
	p->irqaction.handler = sh_mtu2_interrupt;
	p->irqaction.dev_id = p;
	p->irqaction.irq = irq;
	p->irqaction.flags = IRQF_DISABLED | IRQF_TIMER | IRQF_IRQPOLL;

	/* get hold of clock */
	p->clk = clk_get(&p->pdev->dev, cfg->clk);
	if (IS_ERR(p->clk)) {
		pr_err("sh_mtu2: cannot get clock \"%s\"\n", cfg->clk);
		ret = PTR_ERR(p->clk);
		goto err1;
	}

	return sh_mtu2_register(p, cfg->name, cfg->clockevent_rating);
 err1:
	iounmap(p->mapbase);
 err0:
	return ret;
}

static int __devinit sh_mtu2_probe(struct platform_device *pdev)
{
	struct sh_mtu2_priv *p = platform_get_drvdata(pdev);
	struct sh_timer_config *cfg = pdev->dev.platform_data;
	int ret;

	if (p) {
		pr_info("sh_mtu2: %s kept as earlytimer\n", cfg->name);
		return 0;
	}

	p = kmalloc(sizeof(*p), GFP_KERNEL);
	if (p == NULL) {
		dev_err(&pdev->dev, "failed to allocate driver data\n");
		return -ENOMEM;
	}

	ret = sh_mtu2_setup(p, pdev);
	if (ret) {
		kfree(p);
		platform_set_drvdata(pdev, NULL);
	}
	return ret;
}

static int __devexit sh_mtu2_remove(struct platform_device *pdev)
{
	return -EBUSY; /* cannot unregister clockevent */
}

static struct platform_driver sh_mtu2_device_driver = {
	.probe		= sh_mtu2_probe,
	.remove		= __devexit_p(sh_mtu2_remove),
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
module_init(sh_mtu2_init);
module_exit(sh_mtu2_exit);

MODULE_AUTHOR("Magnus Damm");
MODULE_DESCRIPTION("SuperH MTU2 Timer Driver");
MODULE_LICENSE("GPL v2");
