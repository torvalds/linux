// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Driver for MediaTek SoC based RTC
 *
 * Copyright (C) 2017 Sean Wang <sean.wang@mediatek.com>
 */

#include <linux/clk.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/rtc.h>

#define MTK_RTC_DEV KBUILD_MODNAME

#define MTK_RTC_PWRCHK1		0x4
#define	RTC_PWRCHK1_MAGIC	0xc6

#define MTK_RTC_PWRCHK2		0x8
#define	RTC_PWRCHK2_MAGIC	0x9a

#define MTK_RTC_KEY		0xc
#define	RTC_KEY_MAGIC		0x59

#define MTK_RTC_PROT1		0x10
#define	RTC_PROT1_MAGIC		0xa3

#define MTK_RTC_PROT2		0x14
#define	RTC_PROT2_MAGIC		0x57

#define MTK_RTC_PROT3		0x18
#define	RTC_PROT3_MAGIC		0x67

#define MTK_RTC_PROT4		0x1c
#define	RTC_PROT4_MAGIC		0xd2

#define MTK_RTC_CTL		0x20
#define	RTC_RC_STOP		BIT(0)

#define MTK_RTC_DEBNCE		0x2c
#define	RTC_DEBNCE_MASK		GENMASK(2, 0)

#define MTK_RTC_INT		0x30
#define RTC_INT_AL_STA		BIT(4)

/*
 * Ranges from 0x40 to 0x78 provide RTC time setup for year, month,
 * day of month, day of week, hour, minute and second.
 */
#define MTK_RTC_TREG(_t, _f)	(0x40 + (0x4 * (_f)) + ((_t) * 0x20))

#define MTK_RTC_AL_CTL		0x7c
#define	RTC_AL_EN		BIT(0)
#define	RTC_AL_ALL		GENMASK(7, 0)

/*
 * The offset is used in the translation for the year between in struct
 * rtc_time and in hardware register MTK_RTC_TREG(x,MTK_YEA)
 */
#define MTK_RTC_TM_YR_OFFSET	100

/*
 * The lowest value for the valid tm_year. RTC hardware would take incorrectly
 * tm_year 100 as not a leap year and thus it is also required being excluded
 * from the valid options.
 */
#define MTK_RTC_TM_YR_L		(MTK_RTC_TM_YR_OFFSET + 1)

/*
 * The most year the RTC can hold is 99 and the next to 99 in year register
 * would be wraparound to 0, for MT7622.
 */
#define MTK_RTC_HW_YR_LIMIT	99

/* The highest value for the valid tm_year */
#define MTK_RTC_TM_YR_H		(MTK_RTC_TM_YR_OFFSET + MTK_RTC_HW_YR_LIMIT)

/* Simple macro helps to check whether the hardware supports the tm_year */
#define MTK_RTC_TM_YR_VALID(_y)	((_y) >= MTK_RTC_TM_YR_L && \
				 (_y) <= MTK_RTC_TM_YR_H)

/* Types of the function the RTC provides are time counter and alarm. */
enum {
	MTK_TC,
	MTK_AL,
};

/* Indexes are used for the pointer to relevant registers in MTK_RTC_TREG */
enum {
	MTK_YEA,
	MTK_MON,
	MTK_DOM,
	MTK_DOW,
	MTK_HOU,
	MTK_MIN,
	MTK_SEC
};

struct mtk_rtc {
	struct rtc_device *rtc;
	void __iomem *base;
	int irq;
	struct clk *clk;
};

static void mtk_w32(struct mtk_rtc *rtc, u32 reg, u32 val)
{
	writel_relaxed(val, rtc->base + reg);
}

static u32 mtk_r32(struct mtk_rtc *rtc, u32 reg)
{
	return readl_relaxed(rtc->base + reg);
}

static void mtk_rmw(struct mtk_rtc *rtc, u32 reg, u32 mask, u32 set)
{
	u32 val;

	val = mtk_r32(rtc, reg);
	val &= ~mask;
	val |= set;
	mtk_w32(rtc, reg, val);
}

static void mtk_set(struct mtk_rtc *rtc, u32 reg, u32 val)
{
	mtk_rmw(rtc, reg, 0, val);
}

static void mtk_clr(struct mtk_rtc *rtc, u32 reg, u32 val)
{
	mtk_rmw(rtc, reg, val, 0);
}

static void mtk_rtc_hw_init(struct mtk_rtc *hw)
{
	/* The setup of the init sequence is for allowing RTC got to work */
	mtk_w32(hw, MTK_RTC_PWRCHK1, RTC_PWRCHK1_MAGIC);
	mtk_w32(hw, MTK_RTC_PWRCHK2, RTC_PWRCHK2_MAGIC);
	mtk_w32(hw, MTK_RTC_KEY, RTC_KEY_MAGIC);
	mtk_w32(hw, MTK_RTC_PROT1, RTC_PROT1_MAGIC);
	mtk_w32(hw, MTK_RTC_PROT2, RTC_PROT2_MAGIC);
	mtk_w32(hw, MTK_RTC_PROT3, RTC_PROT3_MAGIC);
	mtk_w32(hw, MTK_RTC_PROT4, RTC_PROT4_MAGIC);
	mtk_rmw(hw, MTK_RTC_DEBNCE, RTC_DEBNCE_MASK, 0);
	mtk_clr(hw, MTK_RTC_CTL, RTC_RC_STOP);
}

static void mtk_rtc_get_alarm_or_time(struct mtk_rtc *hw, struct rtc_time *tm,
				      int time_alarm)
{
	u32 year, mon, mday, wday, hour, min, sec;

	/*
	 * Read again until the field of the second is not changed which
	 * ensures all fields in the consistent state. Note that MTK_SEC must
	 * be read first. In this way, it guarantees the others remain not
	 * changed when the results for two MTK_SEC consecutive reads are same.
	 */
	do {
		sec = mtk_r32(hw, MTK_RTC_TREG(time_alarm, MTK_SEC));
		min = mtk_r32(hw, MTK_RTC_TREG(time_alarm, MTK_MIN));
		hour = mtk_r32(hw, MTK_RTC_TREG(time_alarm, MTK_HOU));
		wday = mtk_r32(hw, MTK_RTC_TREG(time_alarm, MTK_DOW));
		mday = mtk_r32(hw, MTK_RTC_TREG(time_alarm, MTK_DOM));
		mon = mtk_r32(hw, MTK_RTC_TREG(time_alarm, MTK_MON));
		year = mtk_r32(hw, MTK_RTC_TREG(time_alarm, MTK_YEA));
	} while (sec != mtk_r32(hw, MTK_RTC_TREG(time_alarm, MTK_SEC)));

	tm->tm_sec  = sec;
	tm->tm_min  = min;
	tm->tm_hour = hour;
	tm->tm_wday = wday;
	tm->tm_mday = mday;
	tm->tm_mon  = mon - 1;

	/* Rebase to the absolute year which userspace queries */
	tm->tm_year = year + MTK_RTC_TM_YR_OFFSET;
}

static void mtk_rtc_set_alarm_or_time(struct mtk_rtc *hw, struct rtc_time *tm,
				      int time_alarm)
{
	u32 year;

	/* Rebase to the relative year which RTC hardware requires */
	year = tm->tm_year - MTK_RTC_TM_YR_OFFSET;

	mtk_w32(hw, MTK_RTC_TREG(time_alarm, MTK_YEA), year);
	mtk_w32(hw, MTK_RTC_TREG(time_alarm, MTK_MON), tm->tm_mon + 1);
	mtk_w32(hw, MTK_RTC_TREG(time_alarm, MTK_DOW), tm->tm_wday);
	mtk_w32(hw, MTK_RTC_TREG(time_alarm, MTK_DOM), tm->tm_mday);
	mtk_w32(hw, MTK_RTC_TREG(time_alarm, MTK_HOU), tm->tm_hour);
	mtk_w32(hw, MTK_RTC_TREG(time_alarm, MTK_MIN), tm->tm_min);
	mtk_w32(hw, MTK_RTC_TREG(time_alarm, MTK_SEC), tm->tm_sec);
}

static irqreturn_t mtk_rtc_alarmirq(int irq, void *id)
{
	struct mtk_rtc *hw = (struct mtk_rtc *)id;
	u32 irq_sta;

	irq_sta = mtk_r32(hw, MTK_RTC_INT);
	if (irq_sta & RTC_INT_AL_STA) {
		/* Stop alarm also implicitly disables the alarm interrupt */
		mtk_w32(hw, MTK_RTC_AL_CTL, 0);
		rtc_update_irq(hw->rtc, 1, RTC_IRQF | RTC_AF);

		/* Ack alarm interrupt status */
		mtk_w32(hw, MTK_RTC_INT, RTC_INT_AL_STA);
		return IRQ_HANDLED;
	}

	return IRQ_NONE;
}

static int mtk_rtc_gettime(struct device *dev, struct rtc_time *tm)
{
	struct mtk_rtc *hw = dev_get_drvdata(dev);

	mtk_rtc_get_alarm_or_time(hw, tm, MTK_TC);

	return 0;
}

static int mtk_rtc_settime(struct device *dev, struct rtc_time *tm)
{
	struct mtk_rtc *hw = dev_get_drvdata(dev);

	if (!MTK_RTC_TM_YR_VALID(tm->tm_year))
		return -EINVAL;

	/* Stop time counter before setting a new one*/
	mtk_set(hw, MTK_RTC_CTL, RTC_RC_STOP);

	mtk_rtc_set_alarm_or_time(hw, tm, MTK_TC);

	/* Restart the time counter */
	mtk_clr(hw, MTK_RTC_CTL, RTC_RC_STOP);

	return 0;
}

static int mtk_rtc_getalarm(struct device *dev, struct rtc_wkalrm *wkalrm)
{
	struct mtk_rtc *hw = dev_get_drvdata(dev);
	struct rtc_time *alrm_tm = &wkalrm->time;

	mtk_rtc_get_alarm_or_time(hw, alrm_tm, MTK_AL);

	wkalrm->enabled = !!(mtk_r32(hw, MTK_RTC_AL_CTL) & RTC_AL_EN);
	wkalrm->pending = !!(mtk_r32(hw, MTK_RTC_INT) & RTC_INT_AL_STA);

	return 0;
}

static int mtk_rtc_setalarm(struct device *dev, struct rtc_wkalrm *wkalrm)
{
	struct mtk_rtc *hw = dev_get_drvdata(dev);
	struct rtc_time *alrm_tm = &wkalrm->time;

	if (!MTK_RTC_TM_YR_VALID(alrm_tm->tm_year))
		return -EINVAL;

	/*
	 * Stop the alarm also implicitly including disables interrupt before
	 * setting a new one.
	 */
	mtk_clr(hw, MTK_RTC_AL_CTL, RTC_AL_EN);

	/*
	 * Avoid contention between mtk_rtc_setalarm and IRQ handler so that
	 * disabling the interrupt and awaiting for pending IRQ handler to
	 * complete.
	 */
	synchronize_irq(hw->irq);

	mtk_rtc_set_alarm_or_time(hw, alrm_tm, MTK_AL);

	/* Restart the alarm with the new setup */
	mtk_w32(hw, MTK_RTC_AL_CTL, RTC_AL_ALL);

	return 0;
}

static const struct rtc_class_ops mtk_rtc_ops = {
	.read_time		= mtk_rtc_gettime,
	.set_time		= mtk_rtc_settime,
	.read_alarm		= mtk_rtc_getalarm,
	.set_alarm		= mtk_rtc_setalarm,
};

static const struct of_device_id mtk_rtc_match[] = {
	{ .compatible = "mediatek,mt7622-rtc" },
	{ .compatible = "mediatek,soc-rtc" },
	{},
};
MODULE_DEVICE_TABLE(of, mtk_rtc_match);

static int mtk_rtc_probe(struct platform_device *pdev)
{
	struct mtk_rtc *hw;
	struct resource *res;
	int ret;

	hw = devm_kzalloc(&pdev->dev, sizeof(*hw), GFP_KERNEL);
	if (!hw)
		return -ENOMEM;

	platform_set_drvdata(pdev, hw);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	hw->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(hw->base))
		return PTR_ERR(hw->base);

	hw->clk = devm_clk_get(&pdev->dev, "rtc");
	if (IS_ERR(hw->clk)) {
		dev_err(&pdev->dev, "No clock\n");
		return PTR_ERR(hw->clk);
	}

	ret = clk_prepare_enable(hw->clk);
	if (ret)
		return ret;

	hw->irq = platform_get_irq(pdev, 0);
	if (hw->irq < 0) {
		dev_err(&pdev->dev, "No IRQ resource\n");
		ret = hw->irq;
		goto err;
	}

	ret = devm_request_irq(&pdev->dev, hw->irq, mtk_rtc_alarmirq,
			       0, dev_name(&pdev->dev), hw);
	if (ret) {
		dev_err(&pdev->dev, "Can't request IRQ\n");
		goto err;
	}

	mtk_rtc_hw_init(hw);

	device_init_wakeup(&pdev->dev, true);

	hw->rtc = devm_rtc_device_register(&pdev->dev, pdev->name,
					   &mtk_rtc_ops, THIS_MODULE);
	if (IS_ERR(hw->rtc)) {
		ret = PTR_ERR(hw->rtc);
		dev_err(&pdev->dev, "Unable to register device\n");
		goto err;
	}

	return 0;
err:
	clk_disable_unprepare(hw->clk);

	return ret;
}

static int mtk_rtc_remove(struct platform_device *pdev)
{
	struct mtk_rtc *hw = platform_get_drvdata(pdev);

	clk_disable_unprepare(hw->clk);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int mtk_rtc_suspend(struct device *dev)
{
	struct mtk_rtc *hw = dev_get_drvdata(dev);

	if (device_may_wakeup(dev))
		enable_irq_wake(hw->irq);

	return 0;
}

static int mtk_rtc_resume(struct device *dev)
{
	struct mtk_rtc *hw = dev_get_drvdata(dev);

	if (device_may_wakeup(dev))
		disable_irq_wake(hw->irq);

	return 0;
}

static SIMPLE_DEV_PM_OPS(mtk_rtc_pm_ops, mtk_rtc_suspend, mtk_rtc_resume);

#define MTK_RTC_PM_OPS (&mtk_rtc_pm_ops)
#else	/* CONFIG_PM */
#define MTK_RTC_PM_OPS NULL
#endif	/* CONFIG_PM */

static struct platform_driver mtk_rtc_driver = {
	.probe	= mtk_rtc_probe,
	.remove	= mtk_rtc_remove,
	.driver = {
		.name = MTK_RTC_DEV,
		.of_match_table = mtk_rtc_match,
		.pm = MTK_RTC_PM_OPS,
	},
};

module_platform_driver(mtk_rtc_driver);

MODULE_DESCRIPTION("MediaTek SoC based RTC Driver");
MODULE_AUTHOR("Sean Wang <sean.wang@mediatek.com>");
MODULE_LICENSE("GPL");
