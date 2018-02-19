/*
 *  Copyright (C) 2009-2010, Lars-Peter Clausen <lars@metafoo.de>
 *  Copyright (C) 2010, Paul Cercueil <paul@crapouillou.net>
 *	 JZ4740 SoC RTC driver
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under  the terms of  the GNU General Public License as published by the
 *  Free Software Foundation;  either version 2 of the License, or (at your
 *  option) any later version.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#include <linux/clk.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/reboot.h>
#include <linux/rtc.h>
#include <linux/slab.h>
#include <linux/spinlock.h>

#define JZ_REG_RTC_CTRL		0x00
#define JZ_REG_RTC_SEC		0x04
#define JZ_REG_RTC_SEC_ALARM	0x08
#define JZ_REG_RTC_REGULATOR	0x0C
#define JZ_REG_RTC_HIBERNATE	0x20
#define JZ_REG_RTC_WAKEUP_FILTER	0x24
#define JZ_REG_RTC_RESET_COUNTER	0x28
#define JZ_REG_RTC_SCRATCHPAD	0x34

/* The following are present on the jz4780 */
#define JZ_REG_RTC_WENR	0x3C
#define JZ_RTC_WENR_WEN	BIT(31)

#define JZ_RTC_CTRL_WRDY	BIT(7)
#define JZ_RTC_CTRL_1HZ		BIT(6)
#define JZ_RTC_CTRL_1HZ_IRQ	BIT(5)
#define JZ_RTC_CTRL_AF		BIT(4)
#define JZ_RTC_CTRL_AF_IRQ	BIT(3)
#define JZ_RTC_CTRL_AE		BIT(2)
#define JZ_RTC_CTRL_ENABLE	BIT(0)

/* Magic value to enable writes on jz4780 */
#define JZ_RTC_WENR_MAGIC	0xA55A

#define JZ_RTC_WAKEUP_FILTER_MASK	0x0000FFE0
#define JZ_RTC_RESET_COUNTER_MASK	0x00000FE0

enum jz4740_rtc_type {
	ID_JZ4740,
	ID_JZ4780,
};

struct jz4740_rtc {
	void __iomem *base;
	enum jz4740_rtc_type type;

	struct rtc_device *rtc;
	struct clk *clk;

	int irq;

	spinlock_t lock;

	unsigned int min_wakeup_pin_assert_time;
	unsigned int reset_pin_assert_time;
};

static struct device *dev_for_power_off;

static inline uint32_t jz4740_rtc_reg_read(struct jz4740_rtc *rtc, size_t reg)
{
	return readl(rtc->base + reg);
}

static int jz4740_rtc_wait_write_ready(struct jz4740_rtc *rtc)
{
	uint32_t ctrl;
	int timeout = 10000;

	do {
		ctrl = jz4740_rtc_reg_read(rtc, JZ_REG_RTC_CTRL);
	} while (!(ctrl & JZ_RTC_CTRL_WRDY) && --timeout);

	return timeout ? 0 : -EIO;
}

static inline int jz4780_rtc_enable_write(struct jz4740_rtc *rtc)
{
	uint32_t ctrl;
	int ret, timeout = 10000;

	ret = jz4740_rtc_wait_write_ready(rtc);
	if (ret != 0)
		return ret;

	writel(JZ_RTC_WENR_MAGIC, rtc->base + JZ_REG_RTC_WENR);

	do {
		ctrl = readl(rtc->base + JZ_REG_RTC_WENR);
	} while (!(ctrl & JZ_RTC_WENR_WEN) && --timeout);

	return timeout ? 0 : -EIO;
}

static inline int jz4740_rtc_reg_write(struct jz4740_rtc *rtc, size_t reg,
	uint32_t val)
{
	int ret = 0;

	if (rtc->type >= ID_JZ4780)
		ret = jz4780_rtc_enable_write(rtc);
	if (ret == 0)
		ret = jz4740_rtc_wait_write_ready(rtc);
	if (ret == 0)
		writel(val, rtc->base + reg);

	return ret;
}

static int jz4740_rtc_ctrl_set_bits(struct jz4740_rtc *rtc, uint32_t mask,
	bool set)
{
	int ret;
	unsigned long flags;
	uint32_t ctrl;

	spin_lock_irqsave(&rtc->lock, flags);

	ctrl = jz4740_rtc_reg_read(rtc, JZ_REG_RTC_CTRL);

	/* Don't clear interrupt flags by accident */
	ctrl |= JZ_RTC_CTRL_1HZ | JZ_RTC_CTRL_AF;

	if (set)
		ctrl |= mask;
	else
		ctrl &= ~mask;

	ret = jz4740_rtc_reg_write(rtc, JZ_REG_RTC_CTRL, ctrl);

	spin_unlock_irqrestore(&rtc->lock, flags);

	return ret;
}

static int jz4740_rtc_read_time(struct device *dev, struct rtc_time *time)
{
	struct jz4740_rtc *rtc = dev_get_drvdata(dev);
	uint32_t secs, secs2;
	int timeout = 5;

	/* If the seconds register is read while it is updated, it can contain a
	 * bogus value. This can be avoided by making sure that two consecutive
	 * reads have the same value.
	 */
	secs = jz4740_rtc_reg_read(rtc, JZ_REG_RTC_SEC);
	secs2 = jz4740_rtc_reg_read(rtc, JZ_REG_RTC_SEC);

	while (secs != secs2 && --timeout) {
		secs = secs2;
		secs2 = jz4740_rtc_reg_read(rtc, JZ_REG_RTC_SEC);
	}

	if (timeout == 0)
		return -EIO;

	rtc_time_to_tm(secs, time);

	return 0;
}

static int jz4740_rtc_set_mmss(struct device *dev, unsigned long secs)
{
	struct jz4740_rtc *rtc = dev_get_drvdata(dev);

	return jz4740_rtc_reg_write(rtc, JZ_REG_RTC_SEC, secs);
}

static int jz4740_rtc_read_alarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	struct jz4740_rtc *rtc = dev_get_drvdata(dev);
	uint32_t secs;
	uint32_t ctrl;

	secs = jz4740_rtc_reg_read(rtc, JZ_REG_RTC_SEC_ALARM);

	ctrl = jz4740_rtc_reg_read(rtc, JZ_REG_RTC_CTRL);

	alrm->enabled = !!(ctrl & JZ_RTC_CTRL_AE);
	alrm->pending = !!(ctrl & JZ_RTC_CTRL_AF);

	rtc_time_to_tm(secs, &alrm->time);

	return rtc_valid_tm(&alrm->time);
}

static int jz4740_rtc_set_alarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	int ret;
	struct jz4740_rtc *rtc = dev_get_drvdata(dev);
	unsigned long secs;

	rtc_tm_to_time(&alrm->time, &secs);

	ret = jz4740_rtc_reg_write(rtc, JZ_REG_RTC_SEC_ALARM, secs);
	if (!ret)
		ret = jz4740_rtc_ctrl_set_bits(rtc,
			JZ_RTC_CTRL_AE | JZ_RTC_CTRL_AF_IRQ, alrm->enabled);

	return ret;
}

static int jz4740_rtc_alarm_irq_enable(struct device *dev, unsigned int enable)
{
	struct jz4740_rtc *rtc = dev_get_drvdata(dev);
	return jz4740_rtc_ctrl_set_bits(rtc, JZ_RTC_CTRL_AF_IRQ, enable);
}

static const struct rtc_class_ops jz4740_rtc_ops = {
	.read_time	= jz4740_rtc_read_time,
	.set_mmss	= jz4740_rtc_set_mmss,
	.read_alarm	= jz4740_rtc_read_alarm,
	.set_alarm	= jz4740_rtc_set_alarm,
	.alarm_irq_enable = jz4740_rtc_alarm_irq_enable,
};

static irqreturn_t jz4740_rtc_irq(int irq, void *data)
{
	struct jz4740_rtc *rtc = data;
	uint32_t ctrl;
	unsigned long events = 0;

	ctrl = jz4740_rtc_reg_read(rtc, JZ_REG_RTC_CTRL);

	if (ctrl & JZ_RTC_CTRL_1HZ)
		events |= (RTC_UF | RTC_IRQF);

	if (ctrl & JZ_RTC_CTRL_AF)
		events |= (RTC_AF | RTC_IRQF);

	rtc_update_irq(rtc->rtc, 1, events);

	jz4740_rtc_ctrl_set_bits(rtc, JZ_RTC_CTRL_1HZ | JZ_RTC_CTRL_AF, false);

	return IRQ_HANDLED;
}

static void jz4740_rtc_poweroff(struct device *dev)
{
	struct jz4740_rtc *rtc = dev_get_drvdata(dev);
	jz4740_rtc_reg_write(rtc, JZ_REG_RTC_HIBERNATE, 1);
}

static void jz4740_rtc_power_off(void)
{
	struct jz4740_rtc *rtc = dev_get_drvdata(dev_for_power_off);
	unsigned long rtc_rate;
	unsigned long wakeup_filter_ticks;
	unsigned long reset_counter_ticks;

	clk_prepare_enable(rtc->clk);

	rtc_rate = clk_get_rate(rtc->clk);

	/*
	 * Set minimum wakeup pin assertion time: 100 ms.
	 * Range is 0 to 2 sec if RTC is clocked at 32 kHz.
	 */
	wakeup_filter_ticks =
		(rtc->min_wakeup_pin_assert_time * rtc_rate) / 1000;
	if (wakeup_filter_ticks < JZ_RTC_WAKEUP_FILTER_MASK)
		wakeup_filter_ticks &= JZ_RTC_WAKEUP_FILTER_MASK;
	else
		wakeup_filter_ticks = JZ_RTC_WAKEUP_FILTER_MASK;
	jz4740_rtc_reg_write(rtc,
			     JZ_REG_RTC_WAKEUP_FILTER, wakeup_filter_ticks);

	/*
	 * Set reset pin low-level assertion time after wakeup: 60 ms.
	 * Range is 0 to 125 ms if RTC is clocked at 32 kHz.
	 */
	reset_counter_ticks = (rtc->reset_pin_assert_time * rtc_rate) / 1000;
	if (reset_counter_ticks < JZ_RTC_RESET_COUNTER_MASK)
		reset_counter_ticks &= JZ_RTC_RESET_COUNTER_MASK;
	else
		reset_counter_ticks = JZ_RTC_RESET_COUNTER_MASK;
	jz4740_rtc_reg_write(rtc,
			     JZ_REG_RTC_RESET_COUNTER, reset_counter_ticks);

	jz4740_rtc_poweroff(dev_for_power_off);
	kernel_halt();
}

static const struct of_device_id jz4740_rtc_of_match[] = {
	{ .compatible = "ingenic,jz4740-rtc", .data = (void *)ID_JZ4740 },
	{ .compatible = "ingenic,jz4780-rtc", .data = (void *)ID_JZ4780 },
	{},
};
MODULE_DEVICE_TABLE(of, jz4740_rtc_of_match);

static int jz4740_rtc_probe(struct platform_device *pdev)
{
	int ret;
	struct jz4740_rtc *rtc;
	uint32_t scratchpad;
	struct resource *mem;
	const struct platform_device_id *id = platform_get_device_id(pdev);
	const struct of_device_id *of_id = of_match_device(
			jz4740_rtc_of_match, &pdev->dev);
	struct device_node *np = pdev->dev.of_node;

	rtc = devm_kzalloc(&pdev->dev, sizeof(*rtc), GFP_KERNEL);
	if (!rtc)
		return -ENOMEM;

	if (of_id)
		rtc->type = (enum jz4740_rtc_type)of_id->data;
	else
		rtc->type = id->driver_data;

	rtc->irq = platform_get_irq(pdev, 0);
	if (rtc->irq < 0) {
		dev_err(&pdev->dev, "Failed to get platform irq\n");
		return -ENOENT;
	}

	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	rtc->base = devm_ioremap_resource(&pdev->dev, mem);
	if (IS_ERR(rtc->base))
		return PTR_ERR(rtc->base);

	rtc->clk = devm_clk_get(&pdev->dev, "rtc");
	if (IS_ERR(rtc->clk)) {
		dev_err(&pdev->dev, "Failed to get RTC clock\n");
		return PTR_ERR(rtc->clk);
	}

	spin_lock_init(&rtc->lock);

	platform_set_drvdata(pdev, rtc);

	device_init_wakeup(&pdev->dev, 1);

	rtc->rtc = devm_rtc_device_register(&pdev->dev, pdev->name,
					&jz4740_rtc_ops, THIS_MODULE);
	if (IS_ERR(rtc->rtc)) {
		ret = PTR_ERR(rtc->rtc);
		dev_err(&pdev->dev, "Failed to register rtc device: %d\n", ret);
		return ret;
	}

	ret = devm_request_irq(&pdev->dev, rtc->irq, jz4740_rtc_irq, 0,
				pdev->name, rtc);
	if (ret) {
		dev_err(&pdev->dev, "Failed to request rtc irq: %d\n", ret);
		return ret;
	}

	scratchpad = jz4740_rtc_reg_read(rtc, JZ_REG_RTC_SCRATCHPAD);
	if (scratchpad != 0x12345678) {
		ret = jz4740_rtc_reg_write(rtc, JZ_REG_RTC_SCRATCHPAD, 0x12345678);
		ret = jz4740_rtc_reg_write(rtc, JZ_REG_RTC_SEC, 0);
		if (ret) {
			dev_err(&pdev->dev, "Could not write to RTC registers\n");
			return ret;
		}
	}

	if (np && of_device_is_system_power_controller(np)) {
		if (!pm_power_off) {
			/* Default: 60ms */
			rtc->reset_pin_assert_time = 60;
			of_property_read_u32(np, "reset-pin-assert-time-ms",
					     &rtc->reset_pin_assert_time);

			/* Default: 100ms */
			rtc->min_wakeup_pin_assert_time = 100;
			of_property_read_u32(np,
					     "min-wakeup-pin-assert-time-ms",
					     &rtc->min_wakeup_pin_assert_time);

			dev_for_power_off = &pdev->dev;
			pm_power_off = jz4740_rtc_power_off;
		} else {
			dev_warn(&pdev->dev,
				 "Poweroff handler already present!\n");
		}
	}

	return 0;
}

#ifdef CONFIG_PM
static int jz4740_rtc_suspend(struct device *dev)
{
	struct jz4740_rtc *rtc = dev_get_drvdata(dev);

	if (device_may_wakeup(dev))
		enable_irq_wake(rtc->irq);
	return 0;
}

static int jz4740_rtc_resume(struct device *dev)
{
	struct jz4740_rtc *rtc = dev_get_drvdata(dev);

	if (device_may_wakeup(dev))
		disable_irq_wake(rtc->irq);
	return 0;
}

static const struct dev_pm_ops jz4740_pm_ops = {
	.suspend = jz4740_rtc_suspend,
	.resume  = jz4740_rtc_resume,
};
#define JZ4740_RTC_PM_OPS (&jz4740_pm_ops)

#else
#define JZ4740_RTC_PM_OPS NULL
#endif  /* CONFIG_PM */

static const struct platform_device_id jz4740_rtc_ids[] = {
	{ "jz4740-rtc", ID_JZ4740 },
	{ "jz4780-rtc", ID_JZ4780 },
	{}
};
MODULE_DEVICE_TABLE(platform, jz4740_rtc_ids);

static struct platform_driver jz4740_rtc_driver = {
	.probe	 = jz4740_rtc_probe,
	.driver	 = {
		.name  = "jz4740-rtc",
		.pm    = JZ4740_RTC_PM_OPS,
		.of_match_table = of_match_ptr(jz4740_rtc_of_match),
	},
	.id_table = jz4740_rtc_ids,
};

module_platform_driver(jz4740_rtc_driver);

MODULE_AUTHOR("Lars-Peter Clausen <lars@metafoo.de>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("RTC driver for the JZ4740 SoC\n");
MODULE_ALIAS("platform:jz4740-rtc");
