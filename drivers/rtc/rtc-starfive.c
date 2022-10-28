// SPDX-License-Identifier: GPL-2.0
/*
 * RTC driver for the StarFive JH7110 SoC
 *
 * Copyright (C) 2021 StarFive Technology Co., Ltd.
 */

#include <asm/delay.h>
#include <linux/bcd.h>
#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/reset.h>
#include <linux/completion.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/iopoll.h>
#include <linux/platform_device.h>
#include <linux/rtc.h>

/* Registers */
#define SFT_RTC_CFG		0x00
#define SFT_RTC_SW_CAL_VALUE	0x04
#define SFT_RTC_HW_CAL_CFG	0x08
#define SFT_RTC_CMP_CFG		0x0C
#define SFT_RTC_IRQ_EN		0x10
#define SFT_RTC_IRQ_EVEVT	0x14
#define SFT_RTC_IRQ_STATUS	0x18
#define SFT_RTC_CAL_VALUE	0x24
#define SFT_RTC_CFG_TIME	0x28
#define SFT_RTC_CFG_DATE	0x2C
#define SFT_RTC_ACT_TIME	0x34
#define SFT_RTC_ACT_DATE	0x38
#define SFT_RTC_TIME		0x3C
#define SFT_RTC_DATE		0x40
#define SFT_RTC_TIME_LATCH	0x44
#define SFT_RTC_DATE_LATCH	0x48

/* RTC_CFG */
#define RTC_CFG_ENABLE_SHIFT	0  /* RW: RTC Enable. */
#define RTC_CFG_CAL_EN_HW_SHIFT	1  /* RW: Enable of hardware calibretion. */
#define RTC_CFG_CAL_SEL_SHIFT	2  /* RW: select the hw/sw calibretion mode.*/
#define RTC_CFG_HOUR_MODE_SHIFT	3  /* RW: time hour mode. 24h|12h */

/* RTC_SW_CAL_VALUE */
#define RTC_SW_CAL_VALUE_MASK	GENMASK(15, 0)
#define RTC_SW_CAL_MAX		RTC_SW_CAL_VALUE_MASK
#define RTC_SW_CAL_MIN		0
#define RTC_TICKS_PER_SEC	32768		/* Number of ticks per second */
#define RTC_PPB_MULT		1000000000LL	/* Multiplier for ppb conversions */

/* RTC_HW_CAL_CFG */
#define RTC_HW_CAL_REF_SEL_SHIFT	0
#define RTC_HW_CAL_FRQ_SEL_SHIFT	1

/* IRQ_EN/IRQ_EVEVT/IRQ_STATUS */
#define RTC_IRQ_CAL_START	BIT(0)
#define RTC_IRQ_CAL_FINISH	BIT(1)
#define RTC_IRQ_CMP		BIT(2)
#define RTC_IRQ_1SEC		BIT(3)
#define RTC_IRQ_ALAEM		BIT(4)
#define RTC_IRQ_EVT_UPDATE_PSE	BIT(31)	/* WO: Enable of update time&&date, IRQ_EVEVT only */
#define RTC_IRQ_ALL		(RTC_IRQ_CAL_START \
				| RTC_IRQ_CAL_FINISH \
				| RTC_IRQ_CMP \
				| RTC_IRQ_1SEC \
				| RTC_IRQ_ALAEM)

/* CAL_VALUE */
#define RTC_CAL_VALUE_MASK	GENMASK(15, 0)

/* CFG_TIME/ACT_TIME/RTC_TIME */
#define TIME_SEC_MASK		GENMASK(6, 0)
#define TIME_MIN_MASK		GENMASK(13, 7)
#define TIME_HOUR_MASK		GENMASK(20, 14)

/* CFG_DATE/ACT_DATE/RTC_DATE */
#define DATE_DAY_MASK		GENMASK(5, 0)
#define DATE_MON_MASK		GENMASK(10, 6)
#define DATE_YEAR_MASK		GENMASK(18, 11)

#define INT_TIMEOUT_US		180

enum RTC_HOUR_MODE {
	RTC_HOUR_MODE_12H = 0,
	RTC_HOUR_MODE_24H = 1
};

enum RTC_CAL_MODE {
	RTC_CAL_MODE_SW = 0,
	RTC_CAL_MODE_HW = 1
};

enum RTC_HW_CAL_REF_MODE {
	RTC_CAL_CLK_REF = 0,
	RTC_CAL_CLK_MARK = 1
};

static const unsigned long refclk_list[] = {
	1000000,
	2000000,
	4000000,
	5927000,
	6000000,
	7200000,
	8000000,
	10250000,
	11059200,
	12000000,
	12288000,
	13560000,
	16000000,
	19200000,
	20000000,
	22118000,
	24000000,
	24567000,
	25000000,
	26000000,
	27000000,
	30000000,
	32000000,
	33868800,
	36000000,
	36860000,
	40000000,
	44000000,
	50000000,
	54000000,
	28224000,
	28000000,
};

struct sft_rtc {
	struct rtc_device *rtc_dev;
	struct completion cal_done;
	struct completion onesec_done;
	struct clk *pclk;
	struct clk *cal_clk;
	struct reset_control *rst_array;
	int hw_cal_map;
	void __iomem *regs;
	int rtc_irq;
	int ms_pulse_irq;
	int one_sec_pulse_irq;
};

static inline void sft_rtc_set_enabled(struct sft_rtc *srtc, bool enabled)
{
	u32 val;

	if (enabled) {
		val = readl(srtc->regs + SFT_RTC_CFG);
		val |= BIT(RTC_CFG_ENABLE_SHIFT);
		writel(val, srtc->regs + SFT_RTC_CFG);
	} else {
		val = readl(srtc->regs + SFT_RTC_CFG);
		val &= ~BIT(RTC_CFG_ENABLE_SHIFT);
		writel(val, srtc->regs + SFT_RTC_CFG);
	}
}

static inline bool sft_rtc_get_enabled(struct sft_rtc *srtc)
{
	return !!(readl(srtc->regs + SFT_RTC_CFG) & BIT(RTC_CFG_ENABLE_SHIFT));
}

static inline void sft_rtc_set_mode(struct sft_rtc *srtc, enum RTC_HOUR_MODE mode)
{
	u32 val;

	val = readl(srtc->regs + SFT_RTC_CFG);
	val |= mode << RTC_CFG_HOUR_MODE_SHIFT;
	writel(val, srtc->regs + SFT_RTC_CFG);
}

static inline int sft_rtc_irq_enable(struct sft_rtc *srtc, u32 irq, bool enable)
{
	u32 val;

	if (!(irq & RTC_IRQ_ALL))
		return -EINVAL;

	if (enable) {
		val = readl(srtc->regs + SFT_RTC_IRQ_EN);
		val |= irq;
		writel(val, srtc->regs + SFT_RTC_IRQ_EN);
	} else {
		val = readl(srtc->regs + SFT_RTC_IRQ_EN);
		val &= ~irq;
		writel(val, srtc->regs + SFT_RTC_IRQ_EN);
	}
	return 0;
}

static inline void
sft_rtc_set_cal_hw_enable(struct sft_rtc *srtc, bool enable)
{
	u32 val;

	if (enable) {
		val = readl(srtc->regs + SFT_RTC_CFG);
		val |= BIT(RTC_CFG_CAL_EN_HW_SHIFT);
		writel(val, srtc->regs + SFT_RTC_CFG);
	} else {
		val = readl(srtc->regs + SFT_RTC_CFG);
		val &= ~BIT(RTC_CFG_CAL_EN_HW_SHIFT);
		writel(val, srtc->regs + SFT_RTC_CFG);
	}
}

static inline void
sft_rtc_set_cal_mode(struct sft_rtc *srtc, enum RTC_CAL_MODE mode)
{
	u32 val;

	val = readl(srtc->regs + SFT_RTC_CFG);
	val |= mode << RTC_CFG_CAL_SEL_SHIFT;
	writel(val, srtc->regs + SFT_RTC_CFG);
}

static int sft_rtc_get_hw_calclk(struct device *dev, unsigned long freq)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(refclk_list); i++)
		if (refclk_list[i] == freq)
			return i;

	dev_err(dev, "refclk: %ldHz do not support.\n", freq);
	return -EINVAL;
}

static inline void sft_rtc_reg2time(struct rtc_time *tm, u32 reg)
{
	tm->tm_hour = bcd2bin(FIELD_GET(TIME_HOUR_MASK, reg));
	tm->tm_min = bcd2bin(FIELD_GET(TIME_MIN_MASK, reg));
	tm->tm_sec = bcd2bin(FIELD_GET(TIME_SEC_MASK, reg));
}

static inline void sft_rtc_reg2date(struct rtc_time *tm, u32 reg)
{
	tm->tm_year = bcd2bin(FIELD_GET(DATE_YEAR_MASK, reg)) + 100;
	tm->tm_mon = bcd2bin(FIELD_GET(DATE_MON_MASK, reg)) - 1;
	tm->tm_mday = bcd2bin(FIELD_GET(DATE_DAY_MASK, reg));
}

static inline u32 sft_rtc_time2reg(struct rtc_time *tm)
{
	return	FIELD_PREP(TIME_HOUR_MASK, bin2bcd(tm->tm_hour)) |
		FIELD_PREP(TIME_MIN_MASK, bin2bcd(tm->tm_min)) |
		FIELD_PREP(TIME_SEC_MASK, bin2bcd(tm->tm_sec));
}

static inline u32 sft_rtc_date2reg(struct rtc_time *tm)
{
	return	FIELD_PREP(DATE_YEAR_MASK, bin2bcd(tm->tm_year - 100)) |
		FIELD_PREP(DATE_MON_MASK, bin2bcd(tm->tm_mon + 1)) |
		FIELD_PREP(DATE_DAY_MASK, bin2bcd(tm->tm_mday));
}

static inline void sft_rtc_update_pulse(struct sft_rtc *srtc)
{
	u32 val;

	val = readl(srtc->regs + SFT_RTC_IRQ_EVEVT);
	val |= RTC_IRQ_EVT_UPDATE_PSE;
	writel(val, srtc->regs + SFT_RTC_IRQ_EVEVT);
}

static irqreturn_t sft_rtc_irq_handler(int irq, void *data)
{
	struct sft_rtc *srtc = data;
	struct timerqueue_node *next;
	u32 irq_flags = 0;
	u32 irq_mask = 0;
	u32 val;
	int ret = 0;

	val = readl(srtc->regs + SFT_RTC_IRQ_EVEVT);
	if (val & RTC_IRQ_CAL_START)
		irq_mask |= RTC_IRQ_CAL_START;

	if (val & RTC_IRQ_CAL_FINISH) {
		irq_mask |= RTC_IRQ_CAL_FINISH;
		complete(&srtc->cal_done);
	}

	if (val & RTC_IRQ_CMP)
		irq_mask |= RTC_IRQ_CMP;

	if (val & RTC_IRQ_1SEC) {
		irq_flags |= RTC_PF;
		irq_mask |= RTC_IRQ_1SEC;
		complete(&srtc->onesec_done);
	}

	if (val & RTC_IRQ_ALAEM) {
		irq_flags |= RTC_AF;
		irq_mask |= RTC_IRQ_ALAEM;

		next = timerqueue_getnext(&srtc->rtc_dev->timerqueue);
		if (next == &srtc->rtc_dev->aie_timer.node)
			dev_info(&srtc->rtc_dev->dev, "alarm expires");
	}

	writel(irq_mask, srtc->regs + SFT_RTC_IRQ_EVEVT);

	/* Wait interrupt flag clear */
	ret = readl_poll_timeout_atomic(srtc->regs + SFT_RTC_IRQ_EVEVT, val,
					(val & irq_mask) == 0, 0, INT_TIMEOUT_US);
	if (ret)
		dev_warn(&srtc->rtc_dev->dev, "fail to clear rtc interrupt flag\n");

	if (irq_flags)
		rtc_update_irq(srtc->rtc_dev, 1, irq_flags | RTC_IRQF);

	return IRQ_HANDLED;
}

static int sft_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
	struct sft_rtc *srtc = dev_get_drvdata(dev);
	u32 val;
	int irq_1sec_state_start, irq_1sec_state_end;

	/* If the RTC is disabled, assume the values are invalid */
	if (!sft_rtc_get_enabled(srtc))
		return -EINVAL;

	irq_1sec_state_start =
		(readl(srtc->regs + SFT_RTC_IRQ_STATUS) & RTC_IRQ_1SEC) == 0 ? 0 : 1;

read_again:
	val = readl(srtc->regs + SFT_RTC_TIME);
	sft_rtc_reg2time(tm, val);

	val = readl(srtc->regs + SFT_RTC_DATE);
	sft_rtc_reg2date(tm, val);

	if (irq_1sec_state_start == 0) {
		irq_1sec_state_end =
			(readl(srtc->regs + SFT_RTC_IRQ_STATUS) & RTC_IRQ_1SEC) == 0 ? 0 : 1;
		if (irq_1sec_state_end == 1) {
			irq_1sec_state_start = 1;
			goto read_again;
		}
	}

	return 0;
}

static int sft_rtc_set_time(struct device *dev, struct rtc_time *tm)
{
	struct sft_rtc *srtc = dev_get_drvdata(dev);
	u32 val;
	int ret;

	val = sft_rtc_time2reg(tm);
	writel(val, srtc->regs + SFT_RTC_CFG_TIME);

	val = sft_rtc_date2reg(tm);
	writel(val, srtc->regs + SFT_RTC_CFG_DATE);

	/* Update pulse */
	sft_rtc_update_pulse(srtc);

	/* Ensure that data is fully written */
	ret = wait_for_completion_interruptible_timeout(&srtc->onesec_done,
							usecs_to_jiffies(120));
	if (ret) {
		dev_warn(dev,
			 "rtc wait for completion interruptible timeout.\n");
	}
	return 0;
}

static int sft_rtc_alarm_irq_enable(struct device *dev, unsigned int enabled)
{
	struct sft_rtc *srtc = dev_get_drvdata(dev);

	return sft_rtc_irq_enable(srtc, RTC_IRQ_ALAEM, enabled);
}

static int sft_rtc_read_alarm(struct device *dev, struct rtc_wkalrm *alarm)
{
	struct sft_rtc *srtc = dev_get_drvdata(dev);
	u32 val;

	val = readl(srtc->regs + SFT_RTC_ACT_TIME);
	sft_rtc_reg2time(&alarm->time, val);

	val = readl(srtc->regs + SFT_RTC_ACT_DATE);
	sft_rtc_reg2date(&alarm->time, val);

	return 0;
}

static int sft_rtc_set_alarm(struct device *dev, struct rtc_wkalrm *alarm)
{
	struct sft_rtc *srtc = dev_get_drvdata(dev);
	u32 val;

	sft_rtc_alarm_irq_enable(dev, 0);

	val = sft_rtc_time2reg(&alarm->time);
	writel(val, srtc->regs + SFT_RTC_ACT_TIME);

	val = sft_rtc_date2reg(&alarm->time);
	writel(val, srtc->regs + SFT_RTC_ACT_DATE);

	sft_rtc_alarm_irq_enable(dev, alarm->enabled);

	return 0;
}

static int sft_rtc_get_offset(struct device *dev, long *offset)
{
	struct sft_rtc *srtc = dev_get_drvdata(dev);
	s64 tmp;
	u32 val;

	val = readl(srtc->regs + SFT_RTC_CAL_VALUE)
			& RTC_SW_CAL_VALUE_MASK;
	val += 1;
	/*
	 * the adjust val range is [0x0000-0xffff],
	 * the default val is 0x7fff (32768-1),mapping offset=0 ;
	 */
	tmp = (s64)val - RTC_TICKS_PER_SEC;
	tmp *= RTC_PPB_MULT;
	tmp = div_s64(tmp, RTC_TICKS_PER_SEC);

	/* Offset value operates in negative way, so swap sign */
	*offset = -tmp;

	return 0;
}

static int sft_rtc_set_offset(struct device *dev, long offset)
{
	struct sft_rtc *srtc = dev_get_drvdata(dev);
	s64 tmp;
	u32 val;

	tmp = offset * RTC_TICKS_PER_SEC;
	tmp = div_s64(tmp, RTC_PPB_MULT);

	tmp = RTC_TICKS_PER_SEC - tmp;
	tmp -= 1;
	if (tmp > RTC_SW_CAL_MAX || tmp < RTC_SW_CAL_MIN) {
		dev_err(dev, "offset is out of range.\n");
		return -EINVAL;
	}

	val = tmp & RTC_SW_CAL_VALUE_MASK;
	/* set software calibration value */
	writel(val, srtc->regs + SFT_RTC_SW_CAL_VALUE);

	/* set CFG_RTC-cal_sel to select calibretion by software. */
	sft_rtc_set_cal_mode(srtc, RTC_CAL_MODE_SW);

	return 0;
}

static __maybe_unused int
sft_rtc_hw_adjustment(struct device *dev, unsigned int enable)
{
	struct sft_rtc *srtc = dev_get_drvdata(dev);
	u32 val;

	if (srtc->hw_cal_map <= 0) {
		dev_err(dev, "fail to get cal-clock-freq.\n");
		return -EFAULT;
	}

	if (enable) {
		sft_rtc_irq_enable(srtc, RTC_IRQ_CAL_FINISH, true);

		/* Set reference clock frequency value */
		val = readl(srtc->regs + SFT_RTC_HW_CAL_CFG);
		val |= (srtc->hw_cal_map << RTC_HW_CAL_FRQ_SEL_SHIFT);
		writel(val, srtc->regs + SFT_RTC_HW_CAL_CFG);

		/* Set CFG_RTC-cal_sel to select calibretion by hardware. */
		sft_rtc_set_cal_mode(srtc, RTC_CAL_MODE_HW);

		/* Set CFG_RTC-cal_en_hw to launch hardware calibretion.*/
		sft_rtc_set_cal_hw_enable(srtc, true);

		wait_for_completion_interruptible_timeout(&srtc->cal_done,
							  usecs_to_jiffies(100));

		sft_rtc_irq_enable(srtc, RTC_IRQ_CAL_FINISH, false);
	} else {
		sft_rtc_set_cal_mode(srtc, RTC_CAL_MODE_SW);
		sft_rtc_set_cal_hw_enable(srtc, false);
	}

	return 0;
}

static int sft_rtc_get_cal_clk(struct device *dev, struct sft_rtc *srtc)
{
	struct device_node *np = dev->of_node;
	unsigned long cal_clk_freq;
	u32 freq;
	int ret;

	srtc->cal_clk = devm_clk_get(dev, "cal_clk");
	if (IS_ERR(srtc->cal_clk))
		return PTR_ERR(srtc->cal_clk);

	clk_prepare_enable(srtc->cal_clk);

	cal_clk_freq = clk_get_rate(srtc->cal_clk);
	if (!cal_clk_freq) {
		dev_warn(dev,
			 "get rate failed, next try to get from dts.\n");
		ret = of_property_read_u32(np, "rtc,cal-clock-freq", &freq);
		if (!ret) {
			cal_clk_freq = (u64)freq;
		} else {
			dev_err(dev,
				"Need rtc,cal-clock-freq define in dts.\n");
			goto err_disable_cal_clk;
		}
	}

	srtc->hw_cal_map = sft_rtc_get_hw_calclk(dev, cal_clk_freq);
	if (srtc->hw_cal_map < 0) {
		ret = srtc->hw_cal_map;
		goto err_disable_cal_clk;
	}

	return 0;

err_disable_cal_clk:
	clk_disable_unprepare(srtc->cal_clk);

	return ret;
}

static int sft_rtc_get_irq(struct platform_device *pdev, struct sft_rtc *srtc)
{
	int ret;

	srtc->rtc_irq = platform_get_irq_byname(pdev, "rtc");
	if (srtc->rtc_irq < 0)
		return -EINVAL;

	ret = devm_request_irq(&pdev->dev, srtc->rtc_irq,
			       sft_rtc_irq_handler, 0,
				KBUILD_MODNAME, srtc);
	if (ret)
		dev_err(&pdev->dev, "Failed to request interrupt, %d\n", ret);

	return ret;
}

static const struct rtc_class_ops starfive_rtc_ops = {
	.read_time		= sft_rtc_read_time,
	.set_time		= sft_rtc_set_time,
	.read_alarm		= sft_rtc_read_alarm,
	.set_alarm		= sft_rtc_set_alarm,
	.alarm_irq_enable	= sft_rtc_alarm_irq_enable,
	.set_offset		= sft_rtc_set_offset,
	.read_offset		= sft_rtc_get_offset,
};

static int sft_rtc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct sft_rtc *srtc;
	struct rtc_time tm;
	struct irq_desc *desc;
	int ret;

	srtc = devm_kzalloc(dev, sizeof(*srtc), GFP_KERNEL);
	if (!srtc)
		return -ENOMEM;

	srtc->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(srtc->regs))
		return PTR_ERR(srtc->regs);

	srtc->pclk = devm_clk_get(dev, "pclk");
	if (IS_ERR(srtc->pclk)) {
		ret = PTR_ERR(srtc->pclk);
		dev_err(dev,
			"Failed to retrieve the peripheral clock, %d\n", ret);
		return ret;
	}

	srtc->rst_array = devm_reset_control_array_get_exclusive(dev);
	if (IS_ERR(srtc->rst_array)) {
		ret = PTR_ERR(srtc->rst_array);
		dev_err(dev,
			"Failed to retrieve the rtc reset, %d\n", ret);
		return ret;
	}

	init_completion(&srtc->cal_done);
	init_completion(&srtc->onesec_done);

	ret = clk_prepare_enable(srtc->pclk);
	if (ret) {
		dev_err(dev,
			"Failed to enable the peripheral clock, %d\n", ret);
		return ret;
	}

	ret = sft_rtc_get_cal_clk(dev, srtc);
	if (ret)
		goto err_disable_pclk;

	ret = reset_control_deassert(srtc->rst_array);
	if (ret) {
		dev_err(dev,
			"Failed to deassert rtc resets, %d\n", ret);
		goto err_disable_cal_clk;
	}

	ret = sft_rtc_get_irq(pdev, srtc);
	if (ret)
		goto err_disable_cal_clk;

	srtc->rtc_dev = devm_rtc_allocate_device(dev);
	if (IS_ERR(srtc->rtc_dev))
		return PTR_ERR(srtc->rtc_dev);

	platform_set_drvdata(pdev, srtc);

	/* The RTC supports 01.01.2001 - 31.12.2099 */
	srtc->rtc_dev->range_min = mktime64(2001,  1,  1,  0,  0,  0);
	srtc->rtc_dev->range_max = mktime64(2099, 12, 31, 23, 59, 59);

	srtc->rtc_dev->ops = &starfive_rtc_ops;
	device_init_wakeup(dev, true);

	desc = irq_to_desc(srtc->rtc_irq);
	irq_desc_get_chip(desc)->flags = IRQCHIP_SKIP_SET_WAKE;

	/* Always use 24-hour mode and keep the RTC values */
	sft_rtc_set_mode(srtc, RTC_HOUR_MODE_24H);

	sft_rtc_set_enabled(srtc, true);

	if (device_property_read_bool(dev, "rtc,hw-adjustment"))
		sft_rtc_hw_adjustment(dev, true);

	/*
	 * If rtc time is out of supported range, reset it to the minimum time.
	 * notice that, actual year = 1900 + tm.tm_year
	 *              actual month = 1 + tm.tm_mon
	 */
	sft_rtc_read_time(dev, &tm);
	if (tm.tm_year < 101 || tm.tm_year > 199 || tm.tm_mon < 0 || tm.tm_mon > 11 ||
	    tm.tm_mday < 1 || tm.tm_mday > 31 || tm.tm_hour < 0 || tm.tm_hour > 23 ||
	    tm.tm_min < 0 || tm.tm_min > 59 || tm.tm_sec < 0 || tm.tm_sec > 59) {
		rtc_time64_to_tm(srtc->rtc_dev->range_min, &tm);
		sft_rtc_set_time(dev, &tm);
	}

	ret = devm_rtc_register_device(srtc->rtc_dev);
	if (ret)
		goto err_disable_wakeup;

	return 0;

err_disable_wakeup:
	device_init_wakeup(dev, false);

err_disable_cal_clk:
	clk_disable_unprepare(srtc->cal_clk);

err_disable_pclk:
	clk_disable_unprepare(srtc->pclk);

	return ret;
}

static int sft_rtc_remove(struct platform_device *pdev)
{
	struct sft_rtc *srtc = platform_get_drvdata(pdev);

	sft_rtc_alarm_irq_enable(&pdev->dev, 0);
	device_init_wakeup(&pdev->dev, 0);

	clk_disable_unprepare(srtc->pclk);
	clk_disable_unprepare(srtc->cal_clk);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int sft_rtc_suspend(struct device *dev)
{
	struct sft_rtc *srtc = dev_get_drvdata(dev);

	if (device_may_wakeup(dev))
		enable_irq_wake(srtc->rtc_irq);

	return 0;
}

static int sft_rtc_resume(struct device *dev)
{
	struct sft_rtc *srtc = dev_get_drvdata(dev);

	if (device_may_wakeup(dev))
		disable_irq_wake(srtc->rtc_irq);

	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(sft_rtc_pm_ops, sft_rtc_suspend, sft_rtc_resume);

static const struct of_device_id sft_rtc_of_match[] = {
	{ .compatible = "starfive,jh7110-rtc" },
	{ },
};
MODULE_DEVICE_TABLE(of, sft_rtc_of_match);

static struct platform_driver starfive_rtc_driver = {
	.driver = {
		.name = "starfive-rtc",
		.of_match_table = sft_rtc_of_match,
		.pm   = &sft_rtc_pm_ops,
	},
	.probe = sft_rtc_probe,
	.remove = sft_rtc_remove,
};
module_platform_driver(starfive_rtc_driver);

MODULE_AUTHOR("Samin Guo <samin.guo@starfivetech.com>");
MODULE_AUTHOR("Hal Feng <hal.feng@starfivetech.com>");
MODULE_DESCRIPTION("StarFive RTC driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:starfive-rtc");
