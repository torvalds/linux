/*
 * Copyright (C) 2017 Spreadtrum Communications Inc.
 *
 * SPDX-License-Identifier: GPL-2.0
 */

#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/rtc.h>

#define SPRD_RTC_SEC_CNT_VALUE		0x0
#define SPRD_RTC_MIN_CNT_VALUE		0x4
#define SPRD_RTC_HOUR_CNT_VALUE		0x8
#define SPRD_RTC_DAY_CNT_VALUE		0xc
#define SPRD_RTC_SEC_CNT_UPD		0x10
#define SPRD_RTC_MIN_CNT_UPD		0x14
#define SPRD_RTC_HOUR_CNT_UPD		0x18
#define SPRD_RTC_DAY_CNT_UPD		0x1c
#define SPRD_RTC_SEC_ALM_UPD		0x20
#define SPRD_RTC_MIN_ALM_UPD		0x24
#define SPRD_RTC_HOUR_ALM_UPD		0x28
#define SPRD_RTC_DAY_ALM_UPD		0x2c
#define SPRD_RTC_INT_EN			0x30
#define SPRD_RTC_INT_RAW_STS		0x34
#define SPRD_RTC_INT_CLR		0x38
#define SPRD_RTC_INT_MASK_STS		0x3C
#define SPRD_RTC_SEC_ALM_VALUE		0x40
#define SPRD_RTC_MIN_ALM_VALUE		0x44
#define SPRD_RTC_HOUR_ALM_VALUE		0x48
#define SPRD_RTC_DAY_ALM_VALUE		0x4c
#define SPRD_RTC_SPG_VALUE		0x50
#define SPRD_RTC_SPG_UPD		0x54
#define SPRD_RTC_SEC_AUXALM_UPD		0x60
#define SPRD_RTC_MIN_AUXALM_UPD		0x64
#define SPRD_RTC_HOUR_AUXALM_UPD	0x68
#define SPRD_RTC_DAY_AUXALM_UPD		0x6c

/* BIT & MASK definition for SPRD_RTC_INT_* registers */
#define SPRD_RTC_SEC_EN			BIT(0)
#define SPRD_RTC_MIN_EN			BIT(1)
#define SPRD_RTC_HOUR_EN		BIT(2)
#define SPRD_RTC_DAY_EN			BIT(3)
#define SPRD_RTC_ALARM_EN		BIT(4)
#define SPRD_RTC_HRS_FORMAT_EN		BIT(5)
#define SPRD_RTC_AUXALM_EN		BIT(6)
#define SPRD_RTC_SPG_UPD_EN		BIT(7)
#define SPRD_RTC_SEC_UPD_EN		BIT(8)
#define SPRD_RTC_MIN_UPD_EN		BIT(9)
#define SPRD_RTC_HOUR_UPD_EN		BIT(10)
#define SPRD_RTC_DAY_UPD_EN		BIT(11)
#define SPRD_RTC_ALMSEC_UPD_EN		BIT(12)
#define SPRD_RTC_ALMMIN_UPD_EN		BIT(13)
#define SPRD_RTC_ALMHOUR_UPD_EN		BIT(14)
#define SPRD_RTC_ALMDAY_UPD_EN		BIT(15)
#define SPRD_RTC_INT_MASK		GENMASK(15, 0)

#define SPRD_RTC_TIME_INT_MASK				\
	(SPRD_RTC_SEC_UPD_EN | SPRD_RTC_MIN_UPD_EN |	\
	 SPRD_RTC_HOUR_UPD_EN | SPRD_RTC_DAY_UPD_EN)

#define SPRD_RTC_ALMTIME_INT_MASK				\
	(SPRD_RTC_ALMSEC_UPD_EN | SPRD_RTC_ALMMIN_UPD_EN |	\
	 SPRD_RTC_ALMHOUR_UPD_EN | SPRD_RTC_ALMDAY_UPD_EN)

#define SPRD_RTC_ALM_INT_MASK			\
	(SPRD_RTC_SEC_EN | SPRD_RTC_MIN_EN |	\
	 SPRD_RTC_HOUR_EN | SPRD_RTC_DAY_EN |	\
	 SPRD_RTC_ALARM_EN | SPRD_RTC_AUXALM_EN)

/* second/minute/hour/day values mask definition */
#define SPRD_RTC_SEC_MASK		GENMASK(5, 0)
#define SPRD_RTC_MIN_MASK		GENMASK(5, 0)
#define SPRD_RTC_HOUR_MASK		GENMASK(4, 0)
#define SPRD_RTC_DAY_MASK		GENMASK(15, 0)

/* alarm lock definition for SPRD_RTC_SPG_UPD register */
#define SPRD_RTC_ALMLOCK_MASK		GENMASK(7, 0)
#define SPRD_RTC_ALM_UNLOCK		0xa5
#define SPRD_RTC_ALM_LOCK		(~SPRD_RTC_ALM_UNLOCK &	\
					 SPRD_RTC_ALMLOCK_MASK)

/* SPG values definition for SPRD_RTC_SPG_UPD register */
#define SPRD_RTC_POWEROFF_ALM_FLAG	BIT(8)
#define SPRD_RTC_POWER_RESET_FLAG	BIT(9)

/* timeout of synchronizing time and alarm registers (us) */
#define SPRD_RTC_POLL_TIMEOUT		200000
#define SPRD_RTC_POLL_DELAY_US		20000

struct sprd_rtc {
	struct rtc_device	*rtc;
	struct regmap		*regmap;
	struct device		*dev;
	u32			base;
	int			irq;
	bool			valid;
};

/*
 * The Spreadtrum RTC controller has 3 groups registers, including time, normal
 * alarm and auxiliary alarm. The time group registers are used to set RTC time,
 * the normal alarm registers are used to set normal alarm, and the auxiliary
 * alarm registers are used to set auxiliary alarm. Both alarm event and
 * auxiliary alarm event can wake up system from deep sleep, but only alarm
 * event can power up system from power down status.
 */
enum sprd_rtc_reg_types {
	SPRD_RTC_TIME,
	SPRD_RTC_ALARM,
	SPRD_RTC_AUX_ALARM,
};

static int sprd_rtc_clear_alarm_ints(struct sprd_rtc *rtc)
{
	return regmap_write(rtc->regmap, rtc->base + SPRD_RTC_INT_CLR,
			    SPRD_RTC_ALM_INT_MASK);
}

static int sprd_rtc_disable_ints(struct sprd_rtc *rtc)
{
	int ret;

	ret = regmap_update_bits(rtc->regmap, rtc->base + SPRD_RTC_INT_EN,
				 SPRD_RTC_INT_MASK, 0);
	if (ret)
		return ret;

	return regmap_write(rtc->regmap, rtc->base + SPRD_RTC_INT_CLR,
			    SPRD_RTC_INT_MASK);
}

static int sprd_rtc_lock_alarm(struct sprd_rtc *rtc, bool lock)
{
	int ret;
	u32 val;

	ret = regmap_read(rtc->regmap, rtc->base + SPRD_RTC_SPG_VALUE, &val);
	if (ret)
		return ret;

	val &= ~(SPRD_RTC_ALMLOCK_MASK | SPRD_RTC_POWEROFF_ALM_FLAG);
	if (lock)
		val |= SPRD_RTC_ALM_LOCK;
	else
		val |= SPRD_RTC_ALM_UNLOCK | SPRD_RTC_POWEROFF_ALM_FLAG;

	ret = regmap_write(rtc->regmap, rtc->base + SPRD_RTC_SPG_UPD, val);
	if (ret)
		return ret;

	/* wait until the SPG value is updated successfully */
	ret = regmap_read_poll_timeout(rtc->regmap,
				       rtc->base + SPRD_RTC_INT_RAW_STS, val,
				       (val & SPRD_RTC_SPG_UPD_EN),
				       SPRD_RTC_POLL_DELAY_US,
				       SPRD_RTC_POLL_TIMEOUT);
	if (ret) {
		dev_err(rtc->dev, "failed to update SPG value:%d\n", ret);
		return ret;
	}

	return 0;
}

static int sprd_rtc_get_secs(struct sprd_rtc *rtc, enum sprd_rtc_reg_types type,
			     time64_t *secs)
{
	u32 sec_reg, min_reg, hour_reg, day_reg;
	u32 val, sec, min, hour, day;
	int ret;

	switch (type) {
	case SPRD_RTC_TIME:
		sec_reg = SPRD_RTC_SEC_CNT_VALUE;
		min_reg = SPRD_RTC_MIN_CNT_VALUE;
		hour_reg = SPRD_RTC_HOUR_CNT_VALUE;
		day_reg = SPRD_RTC_DAY_CNT_VALUE;
		break;
	case SPRD_RTC_ALARM:
		sec_reg = SPRD_RTC_SEC_ALM_VALUE;
		min_reg = SPRD_RTC_MIN_ALM_VALUE;
		hour_reg = SPRD_RTC_HOUR_ALM_VALUE;
		day_reg = SPRD_RTC_DAY_ALM_VALUE;
		break;
	case SPRD_RTC_AUX_ALARM:
		sec_reg = SPRD_RTC_SEC_AUXALM_UPD;
		min_reg = SPRD_RTC_MIN_AUXALM_UPD;
		hour_reg = SPRD_RTC_HOUR_AUXALM_UPD;
		day_reg = SPRD_RTC_DAY_AUXALM_UPD;
		break;
	default:
		return -EINVAL;
	}

	ret = regmap_read(rtc->regmap, rtc->base + sec_reg, &val);
	if (ret)
		return ret;

	sec = val & SPRD_RTC_SEC_MASK;

	ret = regmap_read(rtc->regmap, rtc->base + min_reg, &val);
	if (ret)
		return ret;

	min = val & SPRD_RTC_MIN_MASK;

	ret = regmap_read(rtc->regmap, rtc->base + hour_reg, &val);
	if (ret)
		return ret;

	hour = val & SPRD_RTC_HOUR_MASK;

	ret = regmap_read(rtc->regmap, rtc->base + day_reg, &val);
	if (ret)
		return ret;

	day = val & SPRD_RTC_DAY_MASK;
	*secs = (((time64_t)(day * 24) + hour) * 60 + min) * 60 + sec;
	return 0;
}

static int sprd_rtc_set_secs(struct sprd_rtc *rtc, enum sprd_rtc_reg_types type,
			     time64_t secs)
{
	u32 sec_reg, min_reg, hour_reg, day_reg, sts_mask;
	u32 sec, min, hour, day, val;
	int ret, rem;

	/* convert seconds to RTC time format */
	day = div_s64_rem(secs, 86400, &rem);
	hour = rem / 3600;
	rem -= hour * 3600;
	min = rem / 60;
	sec = rem - min * 60;

	switch (type) {
	case SPRD_RTC_TIME:
		sec_reg = SPRD_RTC_SEC_CNT_UPD;
		min_reg = SPRD_RTC_MIN_CNT_UPD;
		hour_reg = SPRD_RTC_HOUR_CNT_UPD;
		day_reg = SPRD_RTC_DAY_CNT_UPD;
		sts_mask = SPRD_RTC_TIME_INT_MASK;
		break;
	case SPRD_RTC_ALARM:
		sec_reg = SPRD_RTC_SEC_ALM_UPD;
		min_reg = SPRD_RTC_MIN_ALM_UPD;
		hour_reg = SPRD_RTC_HOUR_ALM_UPD;
		day_reg = SPRD_RTC_DAY_ALM_UPD;
		sts_mask = SPRD_RTC_ALMTIME_INT_MASK;
		break;
	case SPRD_RTC_AUX_ALARM:
		sec_reg = SPRD_RTC_SEC_AUXALM_UPD;
		min_reg = SPRD_RTC_MIN_AUXALM_UPD;
		hour_reg = SPRD_RTC_HOUR_AUXALM_UPD;
		day_reg = SPRD_RTC_DAY_AUXALM_UPD;
		sts_mask = 0;
		break;
	default:
		return -EINVAL;
	}

	ret = regmap_write(rtc->regmap, rtc->base + sec_reg, sec);
	if (ret)
		return ret;

	ret = regmap_write(rtc->regmap, rtc->base + min_reg, min);
	if (ret)
		return ret;

	ret = regmap_write(rtc->regmap, rtc->base + hour_reg, hour);
	if (ret)
		return ret;

	ret = regmap_write(rtc->regmap, rtc->base + day_reg, day);
	if (ret)
		return ret;

	if (type == SPRD_RTC_AUX_ALARM)
		return 0;

	/*
	 * Since the time and normal alarm registers are put in always-power-on
	 * region supplied by VDDRTC, then these registers changing time will
	 * be very long, about 125ms. Thus here we should wait until all
	 * values are updated successfully.
	 */
	ret = regmap_read_poll_timeout(rtc->regmap,
				       rtc->base + SPRD_RTC_INT_RAW_STS, val,
				       ((val & sts_mask) == sts_mask),
				       SPRD_RTC_POLL_DELAY_US,
				       SPRD_RTC_POLL_TIMEOUT);
	if (ret < 0) {
		dev_err(rtc->dev, "set time/alarm values timeout\n");
		return ret;
	}

	return regmap_write(rtc->regmap, rtc->base + SPRD_RTC_INT_CLR,
			    sts_mask);
}

static int sprd_rtc_read_aux_alarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	struct sprd_rtc *rtc = dev_get_drvdata(dev);
	time64_t secs;
	u32 val;
	int ret;

	ret = sprd_rtc_get_secs(rtc, SPRD_RTC_AUX_ALARM, &secs);
	if (ret)
		return ret;

	rtc_time64_to_tm(secs, &alrm->time);

	ret = regmap_read(rtc->regmap, rtc->base + SPRD_RTC_INT_EN, &val);
	if (ret)
		return ret;

	alrm->enabled = !!(val & SPRD_RTC_AUXALM_EN);

	ret = regmap_read(rtc->regmap, rtc->base + SPRD_RTC_INT_RAW_STS, &val);
	if (ret)
		return ret;

	alrm->pending = !!(val & SPRD_RTC_AUXALM_EN);
	return 0;
}

static int sprd_rtc_set_aux_alarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	struct sprd_rtc *rtc = dev_get_drvdata(dev);
	time64_t secs = rtc_tm_to_time64(&alrm->time);
	int ret;

	/* clear the auxiliary alarm interrupt status */
	ret = regmap_write(rtc->regmap, rtc->base + SPRD_RTC_INT_CLR,
			   SPRD_RTC_AUXALM_EN);
	if (ret)
		return ret;

	ret = sprd_rtc_set_secs(rtc, SPRD_RTC_AUX_ALARM, secs);
	if (ret)
		return ret;

	if (alrm->enabled) {
		ret = regmap_update_bits(rtc->regmap,
					 rtc->base + SPRD_RTC_INT_EN,
					 SPRD_RTC_AUXALM_EN,
					 SPRD_RTC_AUXALM_EN);
	} else {
		ret = regmap_update_bits(rtc->regmap,
					 rtc->base + SPRD_RTC_INT_EN,
					 SPRD_RTC_AUXALM_EN, 0);
	}

	return ret;
}

static int sprd_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
	struct sprd_rtc *rtc = dev_get_drvdata(dev);
	time64_t secs;
	int ret;

	if (!rtc->valid) {
		dev_warn(dev, "RTC values are invalid\n");
		return -EINVAL;
	}

	ret = sprd_rtc_get_secs(rtc, SPRD_RTC_TIME, &secs);
	if (ret)
		return ret;

	rtc_time64_to_tm(secs, tm);
	return rtc_valid_tm(tm);
}

static int sprd_rtc_set_time(struct device *dev, struct rtc_time *tm)
{
	struct sprd_rtc *rtc = dev_get_drvdata(dev);
	time64_t secs = rtc_tm_to_time64(tm);
	u32 val;
	int ret;

	ret = sprd_rtc_set_secs(rtc, SPRD_RTC_TIME, secs);
	if (ret)
		return ret;

	if (!rtc->valid) {
		/*
		 * Set SPRD_RTC_POWER_RESET_FLAG to indicate now RTC has valid
		 * time values.
		 */
		ret = regmap_update_bits(rtc->regmap,
					 rtc->base + SPRD_RTC_SPG_UPD,
					 SPRD_RTC_POWER_RESET_FLAG,
					 SPRD_RTC_POWER_RESET_FLAG);
		if (ret)
			return ret;

		ret = regmap_read_poll_timeout(rtc->regmap,
					       rtc->base + SPRD_RTC_INT_RAW_STS,
					       val, (val & SPRD_RTC_SPG_UPD_EN),
					       SPRD_RTC_POLL_DELAY_US,
					       SPRD_RTC_POLL_TIMEOUT);
		if (ret) {
			dev_err(rtc->dev, "failed to update SPG value:%d\n",
				ret);
			return ret;
		}

		rtc->valid = true;
	}

	return 0;
}

static int sprd_rtc_read_alarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	struct sprd_rtc *rtc = dev_get_drvdata(dev);
	time64_t secs;
	int ret;
	u32 val;

	/*
	 * If aie_timer is enabled, we should get the normal alarm time.
	 * Otherwise we should get auxiliary alarm time.
	 */
	if (rtc->rtc && rtc->rtc->aie_timer.enabled == 0)
		return sprd_rtc_read_aux_alarm(dev, alrm);

	ret = sprd_rtc_get_secs(rtc, SPRD_RTC_ALARM, &secs);
	if (ret)
		return ret;

	rtc_time64_to_tm(secs, &alrm->time);

	ret = regmap_read(rtc->regmap, rtc->base + SPRD_RTC_INT_EN, &val);
	if (ret)
		return ret;

	alrm->enabled = !!(val & SPRD_RTC_ALARM_EN);

	ret = regmap_read(rtc->regmap, rtc->base + SPRD_RTC_INT_RAW_STS, &val);
	if (ret)
		return ret;

	alrm->pending = !!(val & SPRD_RTC_ALARM_EN);
	return 0;
}

static int sprd_rtc_set_alarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	struct sprd_rtc *rtc = dev_get_drvdata(dev);
	time64_t secs = rtc_tm_to_time64(&alrm->time);
	struct rtc_time aie_time =
		rtc_ktime_to_tm(rtc->rtc->aie_timer.node.expires);
	int ret;

	/*
	 * We have 2 groups alarms: normal alarm and auxiliary alarm. Since
	 * both normal alarm event and auxiliary alarm event can wake up system
	 * from deep sleep, but only alarm event can power up system from power
	 * down status. Moreover we do not need to poll about 125ms when
	 * updating auxiliary alarm registers. Thus we usually set auxiliary
	 * alarm when wake up system from deep sleep, and for other scenarios,
	 * we should set normal alarm with polling status.
	 *
	 * So here we check if the alarm time is set by aie_timer, if yes, we
	 * should set normal alarm, if not, we should set auxiliary alarm which
	 * means it is just a wake event.
	 */
	if (!rtc->rtc->aie_timer.enabled || rtc_tm_sub(&aie_time, &alrm->time))
		return sprd_rtc_set_aux_alarm(dev, alrm);

	/* clear the alarm interrupt status firstly */
	ret = regmap_write(rtc->regmap, rtc->base + SPRD_RTC_INT_CLR,
			   SPRD_RTC_ALARM_EN);
	if (ret)
		return ret;

	ret = sprd_rtc_set_secs(rtc, SPRD_RTC_ALARM, secs);
	if (ret)
		return ret;

	if (alrm->enabled) {
		ret = regmap_update_bits(rtc->regmap,
					 rtc->base + SPRD_RTC_INT_EN,
					 SPRD_RTC_ALARM_EN,
					 SPRD_RTC_ALARM_EN);
		if (ret)
			return ret;

		/* unlock the alarm to enable the alarm function. */
		ret = sprd_rtc_lock_alarm(rtc, false);
	} else {
		regmap_update_bits(rtc->regmap,
				   rtc->base + SPRD_RTC_INT_EN,
				   SPRD_RTC_ALARM_EN, 0);

		/*
		 * Lock the alarm function in case fake alarm event will power
		 * up systems.
		 */
		ret = sprd_rtc_lock_alarm(rtc, true);
	}

	return ret;
}

static int sprd_rtc_alarm_irq_enable(struct device *dev, unsigned int enabled)
{
	struct sprd_rtc *rtc = dev_get_drvdata(dev);
	int ret;

	if (enabled) {
		ret = regmap_update_bits(rtc->regmap,
					 rtc->base + SPRD_RTC_INT_EN,
					 SPRD_RTC_ALARM_EN | SPRD_RTC_AUXALM_EN,
					 SPRD_RTC_ALARM_EN | SPRD_RTC_AUXALM_EN);
		if (ret)
			return ret;

		ret = sprd_rtc_lock_alarm(rtc, false);
	} else {
		regmap_update_bits(rtc->regmap, rtc->base + SPRD_RTC_INT_EN,
				   SPRD_RTC_ALARM_EN | SPRD_RTC_AUXALM_EN, 0);

		ret = sprd_rtc_lock_alarm(rtc, true);
	}

	return ret;
}

static const struct rtc_class_ops sprd_rtc_ops = {
	.read_time = sprd_rtc_read_time,
	.set_time = sprd_rtc_set_time,
	.read_alarm = sprd_rtc_read_alarm,
	.set_alarm = sprd_rtc_set_alarm,
	.alarm_irq_enable = sprd_rtc_alarm_irq_enable,
};

static irqreturn_t sprd_rtc_handler(int irq, void *dev_id)
{
	struct sprd_rtc *rtc = dev_id;
	int ret;

	ret = sprd_rtc_clear_alarm_ints(rtc);
	if (ret)
		return IRQ_RETVAL(ret);

	rtc_update_irq(rtc->rtc, 1, RTC_AF | RTC_IRQF);
	return IRQ_HANDLED;
}

static int sprd_rtc_check_power_down(struct sprd_rtc *rtc)
{
	u32 val;
	int ret;

	ret = regmap_read(rtc->regmap, rtc->base + SPRD_RTC_SPG_VALUE, &val);
	if (ret)
		return ret;

	/*
	 * If the SPRD_RTC_POWER_RESET_FLAG was not set, which means the RTC has
	 * been powered down, so the RTC time values are invalid.
	 */
	rtc->valid = (val & SPRD_RTC_POWER_RESET_FLAG) ? true : false;
	return 0;
}

static int sprd_rtc_probe(struct platform_device *pdev)
{
	struct device_node *node = pdev->dev.of_node;
	struct sprd_rtc *rtc;
	int ret;

	rtc = devm_kzalloc(&pdev->dev, sizeof(*rtc), GFP_KERNEL);
	if (!rtc)
		return -ENOMEM;

	rtc->regmap = dev_get_regmap(pdev->dev.parent, NULL);
	if (!rtc->regmap)
		return -ENODEV;

	ret = of_property_read_u32(node, "reg", &rtc->base);
	if (ret) {
		dev_err(&pdev->dev, "failed to get RTC base address\n");
		return ret;
	}

	rtc->irq = platform_get_irq(pdev, 0);
	if (rtc->irq < 0) {
		dev_err(&pdev->dev, "failed to get RTC irq number\n");
		return rtc->irq;
	}

	rtc->dev = &pdev->dev;
	platform_set_drvdata(pdev, rtc);

	/* clear all RTC interrupts and disable all RTC interrupts */
	ret = sprd_rtc_disable_ints(rtc);
	if (ret) {
		dev_err(&pdev->dev, "failed to disable RTC interrupts\n");
		return ret;
	}

	/* check if RTC time values are valid */
	ret = sprd_rtc_check_power_down(rtc);
	if (ret) {
		dev_err(&pdev->dev, "failed to check RTC time values\n");
		return ret;
	}

	ret = devm_request_threaded_irq(&pdev->dev, rtc->irq, NULL,
					sprd_rtc_handler,
					IRQF_ONESHOT | IRQF_EARLY_RESUME,
					pdev->name, rtc);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to request RTC irq\n");
		return ret;
	}

	rtc->rtc = devm_rtc_device_register(&pdev->dev, pdev->name,
					    &sprd_rtc_ops, THIS_MODULE);
	if (IS_ERR(rtc->rtc))
		return PTR_ERR(rtc->rtc);

	device_init_wakeup(&pdev->dev, 1);
	return 0;
}

static int sprd_rtc_remove(struct platform_device *pdev)
{
	device_init_wakeup(&pdev->dev, 0);
	return 0;
}

static const struct of_device_id sprd_rtc_of_match[] = {
	{ .compatible = "sprd,sc2731-rtc", },
	{ },
};
MODULE_DEVICE_TABLE(of, sprd_rtc_of_match);

static struct platform_driver sprd_rtc_driver = {
	.driver = {
		.name = "sprd-rtc",
		.of_match_table = sprd_rtc_of_match,
	},
	.probe	= sprd_rtc_probe,
	.remove = sprd_rtc_remove,
};
module_platform_driver(sprd_rtc_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Spreadtrum RTC Device Driver");
MODULE_AUTHOR("Baolin Wang <baolin.wang@spreadtrum.com>");
