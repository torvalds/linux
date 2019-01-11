/*
 * An RTC driver for Allwinner A31/A23
 *
 * Copyright (c) 2014, Chen-Yu Tsai <wens@csie.org>
 *
 * based on rtc-sunxi.c
 *
 * An RTC driver for Allwinner A10/A20
 *
 * Copyright (c) 2013, Carlo Caione <carlo.caione@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/rtc.h>
#include <linux/slab.h>
#include <linux/types.h>

/* Control register */
#define SUN6I_LOSC_CTRL				0x0000
#define SUN6I_LOSC_CTRL_KEY			(0x16aa << 16)
#define SUN6I_LOSC_CTRL_ALM_DHMS_ACC		BIT(9)
#define SUN6I_LOSC_CTRL_RTC_HMS_ACC		BIT(8)
#define SUN6I_LOSC_CTRL_RTC_YMD_ACC		BIT(7)
#define SUN6I_LOSC_CTRL_EXT_OSC			BIT(0)
#define SUN6I_LOSC_CTRL_ACC_MASK		GENMASK(9, 7)

#define SUN6I_LOSC_CLK_PRESCAL			0x0008

/* RTC */
#define SUN6I_RTC_YMD				0x0010
#define SUN6I_RTC_HMS				0x0014

/* Alarm 0 (counter) */
#define SUN6I_ALRM_COUNTER			0x0020
#define SUN6I_ALRM_CUR_VAL			0x0024
#define SUN6I_ALRM_EN				0x0028
#define SUN6I_ALRM_EN_CNT_EN			BIT(0)
#define SUN6I_ALRM_IRQ_EN			0x002c
#define SUN6I_ALRM_IRQ_EN_CNT_IRQ_EN		BIT(0)
#define SUN6I_ALRM_IRQ_STA			0x0030
#define SUN6I_ALRM_IRQ_STA_CNT_IRQ_PEND		BIT(0)

/* Alarm 1 (wall clock) */
#define SUN6I_ALRM1_EN				0x0044
#define SUN6I_ALRM1_IRQ_EN			0x0048
#define SUN6I_ALRM1_IRQ_STA			0x004c
#define SUN6I_ALRM1_IRQ_STA_WEEK_IRQ_PEND	BIT(0)

/* Alarm config */
#define SUN6I_ALARM_CONFIG			0x0050
#define SUN6I_ALARM_CONFIG_WAKEUP		BIT(0)

#define SUN6I_LOSC_OUT_GATING			0x0060
#define SUN6I_LOSC_OUT_GATING_EN_OFFSET		0

/*
 * Get date values
 */
#define SUN6I_DATE_GET_DAY_VALUE(x)		((x)  & 0x0000001f)
#define SUN6I_DATE_GET_MON_VALUE(x)		(((x) & 0x00000f00) >> 8)
#define SUN6I_DATE_GET_YEAR_VALUE(x)		(((x) & 0x003f0000) >> 16)
#define SUN6I_LEAP_GET_VALUE(x)			(((x) & 0x00400000) >> 22)

/*
 * Get time values
 */
#define SUN6I_TIME_GET_SEC_VALUE(x)		((x)  & 0x0000003f)
#define SUN6I_TIME_GET_MIN_VALUE(x)		(((x) & 0x00003f00) >> 8)
#define SUN6I_TIME_GET_HOUR_VALUE(x)		(((x) & 0x001f0000) >> 16)

/*
 * Set date values
 */
#define SUN6I_DATE_SET_DAY_VALUE(x)		((x)       & 0x0000001f)
#define SUN6I_DATE_SET_MON_VALUE(x)		((x) <<  8 & 0x00000f00)
#define SUN6I_DATE_SET_YEAR_VALUE(x)		((x) << 16 & 0x003f0000)
#define SUN6I_LEAP_SET_VALUE(x)			((x) << 22 & 0x00400000)

/*
 * Set time values
 */
#define SUN6I_TIME_SET_SEC_VALUE(x)		((x)       & 0x0000003f)
#define SUN6I_TIME_SET_MIN_VALUE(x)		((x) <<  8 & 0x00003f00)
#define SUN6I_TIME_SET_HOUR_VALUE(x)		((x) << 16 & 0x001f0000)

/*
 * The year parameter passed to the driver is usually an offset relative to
 * the year 1900. This macro is used to convert this offset to another one
 * relative to the minimum year allowed by the hardware.
 *
 * The year range is 1970 - 2033. This range is selected to match Allwinner's
 * driver, even though it is somewhat limited.
 */
#define SUN6I_YEAR_MIN				1970
#define SUN6I_YEAR_MAX				2033
#define SUN6I_YEAR_OFF				(SUN6I_YEAR_MIN - 1900)

struct sun6i_rtc_dev {
	struct rtc_device *rtc;
	struct device *dev;
	void __iomem *base;
	int irq;
	unsigned long alarm;

	struct clk_hw hw;
	struct clk_hw *int_osc;
	struct clk *losc;
	struct clk *ext_losc;

	spinlock_t lock;
};

static struct sun6i_rtc_dev *sun6i_rtc;

static unsigned long sun6i_rtc_osc_recalc_rate(struct clk_hw *hw,
					       unsigned long parent_rate)
{
	struct sun6i_rtc_dev *rtc = container_of(hw, struct sun6i_rtc_dev, hw);
	u32 val;

	val = readl(rtc->base + SUN6I_LOSC_CTRL);
	if (val & SUN6I_LOSC_CTRL_EXT_OSC)
		return parent_rate;

	val = readl(rtc->base + SUN6I_LOSC_CLK_PRESCAL);
	val &= GENMASK(4, 0);

	return parent_rate / (val + 1);
}

static u8 sun6i_rtc_osc_get_parent(struct clk_hw *hw)
{
	struct sun6i_rtc_dev *rtc = container_of(hw, struct sun6i_rtc_dev, hw);

	return readl(rtc->base + SUN6I_LOSC_CTRL) & SUN6I_LOSC_CTRL_EXT_OSC;
}

static int sun6i_rtc_osc_set_parent(struct clk_hw *hw, u8 index)
{
	struct sun6i_rtc_dev *rtc = container_of(hw, struct sun6i_rtc_dev, hw);
	unsigned long flags;
	u32 val;

	if (index > 1)
		return -EINVAL;

	spin_lock_irqsave(&rtc->lock, flags);
	val = readl(rtc->base + SUN6I_LOSC_CTRL);
	val &= ~SUN6I_LOSC_CTRL_EXT_OSC;
	val |= SUN6I_LOSC_CTRL_KEY;
	val |= index ? SUN6I_LOSC_CTRL_EXT_OSC : 0;
	writel(val, rtc->base + SUN6I_LOSC_CTRL);
	spin_unlock_irqrestore(&rtc->lock, flags);

	return 0;
}

static const struct clk_ops sun6i_rtc_osc_ops = {
	.recalc_rate	= sun6i_rtc_osc_recalc_rate,

	.get_parent	= sun6i_rtc_osc_get_parent,
	.set_parent	= sun6i_rtc_osc_set_parent,
};

static void __init sun6i_rtc_clk_init(struct device_node *node)
{
	struct clk_hw_onecell_data *clk_data;
	struct sun6i_rtc_dev *rtc;
	struct clk_init_data init = {
		.ops		= &sun6i_rtc_osc_ops,
	};
	const char *clkout_name = "osc32k-out";
	const char *parents[2];

	rtc = kzalloc(sizeof(*rtc), GFP_KERNEL);
	if (!rtc)
		return;

	clk_data = kzalloc(sizeof(*clk_data) + (sizeof(*clk_data->hws) * 2),
			   GFP_KERNEL);
	if (!clk_data) {
		kfree(rtc);
		return;
	}

	spin_lock_init(&rtc->lock);

	rtc->base = of_io_request_and_map(node, 0, of_node_full_name(node));
	if (IS_ERR(rtc->base)) {
		pr_crit("Can't map RTC registers");
		goto err;
	}

	/* Switch to the external, more precise, oscillator */
	writel(SUN6I_LOSC_CTRL_KEY | SUN6I_LOSC_CTRL_EXT_OSC,
	       rtc->base + SUN6I_LOSC_CTRL);

	/* Yes, I know, this is ugly. */
	sun6i_rtc = rtc;

	/* Deal with old DTs */
	if (!of_get_property(node, "clocks", NULL))
		goto err;

	rtc->int_osc = clk_hw_register_fixed_rate_with_accuracy(NULL,
								"rtc-int-osc",
								NULL, 0,
								667000,
								300000000);
	if (IS_ERR(rtc->int_osc)) {
		pr_crit("Couldn't register the internal oscillator\n");
		return;
	}

	parents[0] = clk_hw_get_name(rtc->int_osc);
	parents[1] = of_clk_get_parent_name(node, 0);

	rtc->hw.init = &init;

	init.parent_names = parents;
	init.num_parents = of_clk_get_parent_count(node) + 1;
	of_property_read_string_index(node, "clock-output-names", 0,
				      &init.name);

	rtc->losc = clk_register(NULL, &rtc->hw);
	if (IS_ERR(rtc->losc)) {
		pr_crit("Couldn't register the LOSC clock\n");
		return;
	}

	of_property_read_string_index(node, "clock-output-names", 1,
				      &clkout_name);
	rtc->ext_losc = clk_register_gate(NULL, clkout_name, rtc->hw.init->name,
					  0, rtc->base + SUN6I_LOSC_OUT_GATING,
					  SUN6I_LOSC_OUT_GATING_EN_OFFSET, 0,
					  &rtc->lock);
	if (IS_ERR(rtc->ext_losc)) {
		pr_crit("Couldn't register the LOSC external gate\n");
		return;
	}

	clk_data->num = 2;
	clk_data->hws[0] = &rtc->hw;
	clk_data->hws[1] = __clk_get_hw(rtc->ext_losc);
	of_clk_add_hw_provider(node, of_clk_hw_onecell_get, clk_data);
	return;

err:
	kfree(clk_data);
}
CLK_OF_DECLARE_DRIVER(sun6i_rtc_clk, "allwinner,sun6i-a31-rtc",
		      sun6i_rtc_clk_init);

static irqreturn_t sun6i_rtc_alarmirq(int irq, void *id)
{
	struct sun6i_rtc_dev *chip = (struct sun6i_rtc_dev *) id;
	irqreturn_t ret = IRQ_NONE;
	u32 val;

	spin_lock(&chip->lock);
	val = readl(chip->base + SUN6I_ALRM_IRQ_STA);

	if (val & SUN6I_ALRM_IRQ_STA_CNT_IRQ_PEND) {
		val |= SUN6I_ALRM_IRQ_STA_CNT_IRQ_PEND;
		writel(val, chip->base + SUN6I_ALRM_IRQ_STA);

		rtc_update_irq(chip->rtc, 1, RTC_AF | RTC_IRQF);

		ret = IRQ_HANDLED;
	}
	spin_unlock(&chip->lock);

	return ret;
}

static void sun6i_rtc_setaie(int to, struct sun6i_rtc_dev *chip)
{
	u32 alrm_val = 0;
	u32 alrm_irq_val = 0;
	u32 alrm_wake_val = 0;
	unsigned long flags;

	if (to) {
		alrm_val = SUN6I_ALRM_EN_CNT_EN;
		alrm_irq_val = SUN6I_ALRM_IRQ_EN_CNT_IRQ_EN;
		alrm_wake_val = SUN6I_ALARM_CONFIG_WAKEUP;
	} else {
		writel(SUN6I_ALRM_IRQ_STA_CNT_IRQ_PEND,
		       chip->base + SUN6I_ALRM_IRQ_STA);
	}

	spin_lock_irqsave(&chip->lock, flags);
	writel(alrm_val, chip->base + SUN6I_ALRM_EN);
	writel(alrm_irq_val, chip->base + SUN6I_ALRM_IRQ_EN);
	writel(alrm_wake_val, chip->base + SUN6I_ALARM_CONFIG);
	spin_unlock_irqrestore(&chip->lock, flags);
}

static int sun6i_rtc_gettime(struct device *dev, struct rtc_time *rtc_tm)
{
	struct sun6i_rtc_dev *chip = dev_get_drvdata(dev);
	u32 date, time;

	/*
	 * read again in case it changes
	 */
	do {
		date = readl(chip->base + SUN6I_RTC_YMD);
		time = readl(chip->base + SUN6I_RTC_HMS);
	} while ((date != readl(chip->base + SUN6I_RTC_YMD)) ||
		 (time != readl(chip->base + SUN6I_RTC_HMS)));

	rtc_tm->tm_sec  = SUN6I_TIME_GET_SEC_VALUE(time);
	rtc_tm->tm_min  = SUN6I_TIME_GET_MIN_VALUE(time);
	rtc_tm->tm_hour = SUN6I_TIME_GET_HOUR_VALUE(time);

	rtc_tm->tm_mday = SUN6I_DATE_GET_DAY_VALUE(date);
	rtc_tm->tm_mon  = SUN6I_DATE_GET_MON_VALUE(date);
	rtc_tm->tm_year = SUN6I_DATE_GET_YEAR_VALUE(date);

	rtc_tm->tm_mon  -= 1;

	/*
	 * switch from (data_year->min)-relative offset to
	 * a (1900)-relative one
	 */
	rtc_tm->tm_year += SUN6I_YEAR_OFF;

	return 0;
}

static int sun6i_rtc_getalarm(struct device *dev, struct rtc_wkalrm *wkalrm)
{
	struct sun6i_rtc_dev *chip = dev_get_drvdata(dev);
	unsigned long flags;
	u32 alrm_st;
	u32 alrm_en;

	spin_lock_irqsave(&chip->lock, flags);
	alrm_en = readl(chip->base + SUN6I_ALRM_IRQ_EN);
	alrm_st = readl(chip->base + SUN6I_ALRM_IRQ_STA);
	spin_unlock_irqrestore(&chip->lock, flags);

	wkalrm->enabled = !!(alrm_en & SUN6I_ALRM_EN_CNT_EN);
	wkalrm->pending = !!(alrm_st & SUN6I_ALRM_EN_CNT_EN);
	rtc_time_to_tm(chip->alarm, &wkalrm->time);

	return 0;
}

static int sun6i_rtc_setalarm(struct device *dev, struct rtc_wkalrm *wkalrm)
{
	struct sun6i_rtc_dev *chip = dev_get_drvdata(dev);
	struct rtc_time *alrm_tm = &wkalrm->time;
	struct rtc_time tm_now;
	unsigned long time_now = 0;
	unsigned long time_set = 0;
	unsigned long time_gap = 0;
	int ret = 0;

	ret = sun6i_rtc_gettime(dev, &tm_now);
	if (ret < 0) {
		dev_err(dev, "Error in getting time\n");
		return -EINVAL;
	}

	rtc_tm_to_time(alrm_tm, &time_set);
	rtc_tm_to_time(&tm_now, &time_now);
	if (time_set <= time_now) {
		dev_err(dev, "Date to set in the past\n");
		return -EINVAL;
	}

	time_gap = time_set - time_now;

	if (time_gap > U32_MAX) {
		dev_err(dev, "Date too far in the future\n");
		return -EINVAL;
	}

	sun6i_rtc_setaie(0, chip);
	writel(0, chip->base + SUN6I_ALRM_COUNTER);
	usleep_range(100, 300);

	writel(time_gap, chip->base + SUN6I_ALRM_COUNTER);
	chip->alarm = time_set;

	sun6i_rtc_setaie(wkalrm->enabled, chip);

	return 0;
}

static int sun6i_rtc_wait(struct sun6i_rtc_dev *chip, int offset,
			  unsigned int mask, unsigned int ms_timeout)
{
	const unsigned long timeout = jiffies + msecs_to_jiffies(ms_timeout);
	u32 reg;

	do {
		reg = readl(chip->base + offset);
		reg &= mask;

		if (!reg)
			return 0;

	} while (time_before(jiffies, timeout));

	return -ETIMEDOUT;
}

static int sun6i_rtc_settime(struct device *dev, struct rtc_time *rtc_tm)
{
	struct sun6i_rtc_dev *chip = dev_get_drvdata(dev);
	u32 date = 0;
	u32 time = 0;
	int year;

	year = rtc_tm->tm_year + 1900;
	if (year < SUN6I_YEAR_MIN || year > SUN6I_YEAR_MAX) {
		dev_err(dev, "rtc only supports year in range %d - %d\n",
			SUN6I_YEAR_MIN, SUN6I_YEAR_MAX);
		return -EINVAL;
	}

	rtc_tm->tm_year -= SUN6I_YEAR_OFF;
	rtc_tm->tm_mon += 1;

	date = SUN6I_DATE_SET_DAY_VALUE(rtc_tm->tm_mday) |
		SUN6I_DATE_SET_MON_VALUE(rtc_tm->tm_mon)  |
		SUN6I_DATE_SET_YEAR_VALUE(rtc_tm->tm_year);

	if (is_leap_year(year))
		date |= SUN6I_LEAP_SET_VALUE(1);

	time = SUN6I_TIME_SET_SEC_VALUE(rtc_tm->tm_sec)  |
		SUN6I_TIME_SET_MIN_VALUE(rtc_tm->tm_min)  |
		SUN6I_TIME_SET_HOUR_VALUE(rtc_tm->tm_hour);

	/* Check whether registers are writable */
	if (sun6i_rtc_wait(chip, SUN6I_LOSC_CTRL,
			   SUN6I_LOSC_CTRL_ACC_MASK, 50)) {
		dev_err(dev, "rtc is still busy.\n");
		return -EBUSY;
	}

	writel(time, chip->base + SUN6I_RTC_HMS);

	/*
	 * After writing the RTC HH-MM-SS register, the
	 * SUN6I_LOSC_CTRL_RTC_HMS_ACC bit is set and it will not
	 * be cleared until the real writing operation is finished
	 */

	if (sun6i_rtc_wait(chip, SUN6I_LOSC_CTRL,
			   SUN6I_LOSC_CTRL_RTC_HMS_ACC, 50)) {
		dev_err(dev, "Failed to set rtc time.\n");
		return -ETIMEDOUT;
	}

	writel(date, chip->base + SUN6I_RTC_YMD);

	/*
	 * After writing the RTC YY-MM-DD register, the
	 * SUN6I_LOSC_CTRL_RTC_YMD_ACC bit is set and it will not
	 * be cleared until the real writing operation is finished
	 */

	if (sun6i_rtc_wait(chip, SUN6I_LOSC_CTRL,
			   SUN6I_LOSC_CTRL_RTC_YMD_ACC, 50)) {
		dev_err(dev, "Failed to set rtc time.\n");
		return -ETIMEDOUT;
	}

	return 0;
}

static int sun6i_rtc_alarm_irq_enable(struct device *dev, unsigned int enabled)
{
	struct sun6i_rtc_dev *chip = dev_get_drvdata(dev);

	if (!enabled)
		sun6i_rtc_setaie(enabled, chip);

	return 0;
}

static const struct rtc_class_ops sun6i_rtc_ops = {
	.read_time		= sun6i_rtc_gettime,
	.set_time		= sun6i_rtc_settime,
	.read_alarm		= sun6i_rtc_getalarm,
	.set_alarm		= sun6i_rtc_setalarm,
	.alarm_irq_enable	= sun6i_rtc_alarm_irq_enable
};

static int sun6i_rtc_probe(struct platform_device *pdev)
{
	struct sun6i_rtc_dev *chip = sun6i_rtc;
	int ret;

	if (!chip)
		return -ENODEV;

	platform_set_drvdata(pdev, chip);
	chip->dev = &pdev->dev;

	chip->irq = platform_get_irq(pdev, 0);
	if (chip->irq < 0) {
		dev_err(&pdev->dev, "No IRQ resource\n");
		return chip->irq;
	}

	ret = devm_request_irq(&pdev->dev, chip->irq, sun6i_rtc_alarmirq,
			       0, dev_name(&pdev->dev), chip);
	if (ret) {
		dev_err(&pdev->dev, "Could not request IRQ\n");
		return ret;
	}

	/* clear the alarm counter value */
	writel(0, chip->base + SUN6I_ALRM_COUNTER);

	/* disable counter alarm */
	writel(0, chip->base + SUN6I_ALRM_EN);

	/* disable counter alarm interrupt */
	writel(0, chip->base + SUN6I_ALRM_IRQ_EN);

	/* disable week alarm */
	writel(0, chip->base + SUN6I_ALRM1_EN);

	/* disable week alarm interrupt */
	writel(0, chip->base + SUN6I_ALRM1_IRQ_EN);

	/* clear counter alarm pending interrupts */
	writel(SUN6I_ALRM_IRQ_STA_CNT_IRQ_PEND,
	       chip->base + SUN6I_ALRM_IRQ_STA);

	/* clear week alarm pending interrupts */
	writel(SUN6I_ALRM1_IRQ_STA_WEEK_IRQ_PEND,
	       chip->base + SUN6I_ALRM1_IRQ_STA);

	/* disable alarm wakeup */
	writel(0, chip->base + SUN6I_ALARM_CONFIG);

	clk_prepare_enable(chip->losc);

	chip->rtc = devm_rtc_device_register(&pdev->dev, "rtc-sun6i",
					     &sun6i_rtc_ops, THIS_MODULE);
	if (IS_ERR(chip->rtc)) {
		dev_err(&pdev->dev, "unable to register device\n");
		return PTR_ERR(chip->rtc);
	}

	dev_info(&pdev->dev, "RTC enabled\n");

	return 0;
}

static const struct of_device_id sun6i_rtc_dt_ids[] = {
	{ .compatible = "allwinner,sun6i-a31-rtc" },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, sun6i_rtc_dt_ids);

static struct platform_driver sun6i_rtc_driver = {
	.probe		= sun6i_rtc_probe,
	.driver		= {
		.name		= "sun6i-rtc",
		.of_match_table = sun6i_rtc_dt_ids,
	},
};
builtin_platform_driver(sun6i_rtc_driver);
