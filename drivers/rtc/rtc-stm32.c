/*
 * Copyright (C) Amelie Delaunay 2016
 * Author:  Amelie Delaunay <amelie.delaunay@st.com>
 * License terms:  GNU General Public License (GPL), version 2
 */

#include <linux/bcd.h>
#include <linux/clk.h>
#include <linux/iopoll.h>
#include <linux/ioport.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/regmap.h>
#include <linux/rtc.h>

#define DRIVER_NAME "stm32_rtc"

/* STM32 RTC registers */
#define STM32_RTC_TR		0x00
#define STM32_RTC_DR		0x04
#define STM32_RTC_CR		0x08
#define STM32_RTC_ISR		0x0C
#define STM32_RTC_PRER		0x10
#define STM32_RTC_ALRMAR	0x1C
#define STM32_RTC_WPR		0x24

/* STM32_RTC_TR bit fields  */
#define STM32_RTC_TR_SEC_SHIFT		0
#define STM32_RTC_TR_SEC		GENMASK(6, 0)
#define STM32_RTC_TR_MIN_SHIFT		8
#define STM32_RTC_TR_MIN		GENMASK(14, 8)
#define STM32_RTC_TR_HOUR_SHIFT		16
#define STM32_RTC_TR_HOUR		GENMASK(21, 16)

/* STM32_RTC_DR bit fields */
#define STM32_RTC_DR_DATE_SHIFT		0
#define STM32_RTC_DR_DATE		GENMASK(5, 0)
#define STM32_RTC_DR_MONTH_SHIFT	8
#define STM32_RTC_DR_MONTH		GENMASK(12, 8)
#define STM32_RTC_DR_WDAY_SHIFT		13
#define STM32_RTC_DR_WDAY		GENMASK(15, 13)
#define STM32_RTC_DR_YEAR_SHIFT		16
#define STM32_RTC_DR_YEAR		GENMASK(23, 16)

/* STM32_RTC_CR bit fields */
#define STM32_RTC_CR_FMT		BIT(6)
#define STM32_RTC_CR_ALRAE		BIT(8)
#define STM32_RTC_CR_ALRAIE		BIT(12)

/* STM32_RTC_ISR bit fields */
#define STM32_RTC_ISR_ALRAWF		BIT(0)
#define STM32_RTC_ISR_INITS		BIT(4)
#define STM32_RTC_ISR_RSF		BIT(5)
#define STM32_RTC_ISR_INITF		BIT(6)
#define STM32_RTC_ISR_INIT		BIT(7)
#define STM32_RTC_ISR_ALRAF		BIT(8)

/* STM32_RTC_PRER bit fields */
#define STM32_RTC_PRER_PRED_S_SHIFT	0
#define STM32_RTC_PRER_PRED_S		GENMASK(14, 0)
#define STM32_RTC_PRER_PRED_A_SHIFT	16
#define STM32_RTC_PRER_PRED_A		GENMASK(22, 16)

/* STM32_RTC_ALRMAR and STM32_RTC_ALRMBR bit fields */
#define STM32_RTC_ALRMXR_SEC_SHIFT	0
#define STM32_RTC_ALRMXR_SEC		GENMASK(6, 0)
#define STM32_RTC_ALRMXR_SEC_MASK	BIT(7)
#define STM32_RTC_ALRMXR_MIN_SHIFT	8
#define STM32_RTC_ALRMXR_MIN		GENMASK(14, 8)
#define STM32_RTC_ALRMXR_MIN_MASK	BIT(15)
#define STM32_RTC_ALRMXR_HOUR_SHIFT	16
#define STM32_RTC_ALRMXR_HOUR		GENMASK(21, 16)
#define STM32_RTC_ALRMXR_PM		BIT(22)
#define STM32_RTC_ALRMXR_HOUR_MASK	BIT(23)
#define STM32_RTC_ALRMXR_DATE_SHIFT	24
#define STM32_RTC_ALRMXR_DATE		GENMASK(29, 24)
#define STM32_RTC_ALRMXR_WDSEL		BIT(30)
#define STM32_RTC_ALRMXR_WDAY_SHIFT	24
#define STM32_RTC_ALRMXR_WDAY		GENMASK(27, 24)
#define STM32_RTC_ALRMXR_DATE_MASK	BIT(31)

/* STM32_RTC_WPR key constants */
#define RTC_WPR_1ST_KEY			0xCA
#define RTC_WPR_2ND_KEY			0x53
#define RTC_WPR_WRONG_KEY		0xFF

/*
 * RTC registers are protected against parasitic write access.
 * PWR_CR_DBP bit must be set to enable write access to RTC registers.
 */
/* STM32_PWR_CR */
#define PWR_CR				0x00
/* STM32_PWR_CR bit field */
#define PWR_CR_DBP			BIT(8)

struct stm32_rtc_data {
	bool has_pclk;
};

struct stm32_rtc {
	struct rtc_device *rtc_dev;
	void __iomem *base;
	struct regmap *dbp;
	struct stm32_rtc_data *data;
	struct clk *pclk;
	struct clk *rtc_ck;
	int irq_alarm;
};

static void stm32_rtc_wpr_unlock(struct stm32_rtc *rtc)
{
	writel_relaxed(RTC_WPR_1ST_KEY, rtc->base + STM32_RTC_WPR);
	writel_relaxed(RTC_WPR_2ND_KEY, rtc->base + STM32_RTC_WPR);
}

static void stm32_rtc_wpr_lock(struct stm32_rtc *rtc)
{
	writel_relaxed(RTC_WPR_WRONG_KEY, rtc->base + STM32_RTC_WPR);
}

static int stm32_rtc_enter_init_mode(struct stm32_rtc *rtc)
{
	unsigned int isr = readl_relaxed(rtc->base + STM32_RTC_ISR);

	if (!(isr & STM32_RTC_ISR_INITF)) {
		isr |= STM32_RTC_ISR_INIT;
		writel_relaxed(isr, rtc->base + STM32_RTC_ISR);

		/*
		 * It takes around 2 rtc_ck clock cycles to enter in
		 * initialization phase mode (and have INITF flag set). As
		 * slowest rtc_ck frequency may be 32kHz and highest should be
		 * 1MHz, we poll every 10 us with a timeout of 100ms.
		 */
		return readl_relaxed_poll_timeout_atomic(
					rtc->base + STM32_RTC_ISR,
					isr, (isr & STM32_RTC_ISR_INITF),
					10, 100000);
	}

	return 0;
}

static void stm32_rtc_exit_init_mode(struct stm32_rtc *rtc)
{
	unsigned int isr = readl_relaxed(rtc->base + STM32_RTC_ISR);

	isr &= ~STM32_RTC_ISR_INIT;
	writel_relaxed(isr, rtc->base + STM32_RTC_ISR);
}

static int stm32_rtc_wait_sync(struct stm32_rtc *rtc)
{
	unsigned int isr = readl_relaxed(rtc->base + STM32_RTC_ISR);

	isr &= ~STM32_RTC_ISR_RSF;
	writel_relaxed(isr, rtc->base + STM32_RTC_ISR);

	/*
	 * Wait for RSF to be set to ensure the calendar registers are
	 * synchronised, it takes around 2 rtc_ck clock cycles
	 */
	return readl_relaxed_poll_timeout_atomic(rtc->base + STM32_RTC_ISR,
						 isr,
						 (isr & STM32_RTC_ISR_RSF),
						 10, 100000);
}

static irqreturn_t stm32_rtc_alarm_irq(int irq, void *dev_id)
{
	struct stm32_rtc *rtc = (struct stm32_rtc *)dev_id;
	unsigned int isr, cr;

	mutex_lock(&rtc->rtc_dev->ops_lock);

	isr = readl_relaxed(rtc->base + STM32_RTC_ISR);
	cr = readl_relaxed(rtc->base + STM32_RTC_CR);

	if ((isr & STM32_RTC_ISR_ALRAF) &&
	    (cr & STM32_RTC_CR_ALRAIE)) {
		/* Alarm A flag - Alarm interrupt */
		dev_dbg(&rtc->rtc_dev->dev, "Alarm occurred\n");

		/* Pass event to the kernel */
		rtc_update_irq(rtc->rtc_dev, 1, RTC_IRQF | RTC_AF);

		/* Clear event flag, otherwise new events won't be received */
		writel_relaxed(isr & ~STM32_RTC_ISR_ALRAF,
			       rtc->base + STM32_RTC_ISR);
	}

	mutex_unlock(&rtc->rtc_dev->ops_lock);

	return IRQ_HANDLED;
}

/* Convert rtc_time structure from bin to bcd format */
static void tm2bcd(struct rtc_time *tm)
{
	tm->tm_sec = bin2bcd(tm->tm_sec);
	tm->tm_min = bin2bcd(tm->tm_min);
	tm->tm_hour = bin2bcd(tm->tm_hour);

	tm->tm_mday = bin2bcd(tm->tm_mday);
	tm->tm_mon = bin2bcd(tm->tm_mon + 1);
	tm->tm_year = bin2bcd(tm->tm_year - 100);
	/*
	 * Number of days since Sunday
	 * - on kernel side, 0=Sunday...6=Saturday
	 * - on rtc side, 0=invalid,1=Monday...7=Sunday
	 */
	tm->tm_wday = (!tm->tm_wday) ? 7 : tm->tm_wday;
}

/* Convert rtc_time structure from bcd to bin format */
static void bcd2tm(struct rtc_time *tm)
{
	tm->tm_sec = bcd2bin(tm->tm_sec);
	tm->tm_min = bcd2bin(tm->tm_min);
	tm->tm_hour = bcd2bin(tm->tm_hour);

	tm->tm_mday = bcd2bin(tm->tm_mday);
	tm->tm_mon = bcd2bin(tm->tm_mon) - 1;
	tm->tm_year = bcd2bin(tm->tm_year) + 100;
	/*
	 * Number of days since Sunday
	 * - on kernel side, 0=Sunday...6=Saturday
	 * - on rtc side, 0=invalid,1=Monday...7=Sunday
	 */
	tm->tm_wday %= 7;
}

static int stm32_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
	struct stm32_rtc *rtc = dev_get_drvdata(dev);
	unsigned int tr, dr;

	/* Time and Date in BCD format */
	tr = readl_relaxed(rtc->base + STM32_RTC_TR);
	dr = readl_relaxed(rtc->base + STM32_RTC_DR);

	tm->tm_sec = (tr & STM32_RTC_TR_SEC) >> STM32_RTC_TR_SEC_SHIFT;
	tm->tm_min = (tr & STM32_RTC_TR_MIN) >> STM32_RTC_TR_MIN_SHIFT;
	tm->tm_hour = (tr & STM32_RTC_TR_HOUR) >> STM32_RTC_TR_HOUR_SHIFT;

	tm->tm_mday = (dr & STM32_RTC_DR_DATE) >> STM32_RTC_DR_DATE_SHIFT;
	tm->tm_mon = (dr & STM32_RTC_DR_MONTH) >> STM32_RTC_DR_MONTH_SHIFT;
	tm->tm_year = (dr & STM32_RTC_DR_YEAR) >> STM32_RTC_DR_YEAR_SHIFT;
	tm->tm_wday = (dr & STM32_RTC_DR_WDAY) >> STM32_RTC_DR_WDAY_SHIFT;

	/* We don't report tm_yday and tm_isdst */

	bcd2tm(tm);

	return 0;
}

static int stm32_rtc_set_time(struct device *dev, struct rtc_time *tm)
{
	struct stm32_rtc *rtc = dev_get_drvdata(dev);
	unsigned int tr, dr;
	int ret = 0;

	tm2bcd(tm);

	/* Time in BCD format */
	tr = ((tm->tm_sec << STM32_RTC_TR_SEC_SHIFT) & STM32_RTC_TR_SEC) |
	     ((tm->tm_min << STM32_RTC_TR_MIN_SHIFT) & STM32_RTC_TR_MIN) |
	     ((tm->tm_hour << STM32_RTC_TR_HOUR_SHIFT) & STM32_RTC_TR_HOUR);

	/* Date in BCD format */
	dr = ((tm->tm_mday << STM32_RTC_DR_DATE_SHIFT) & STM32_RTC_DR_DATE) |
	     ((tm->tm_mon << STM32_RTC_DR_MONTH_SHIFT) & STM32_RTC_DR_MONTH) |
	     ((tm->tm_year << STM32_RTC_DR_YEAR_SHIFT) & STM32_RTC_DR_YEAR) |
	     ((tm->tm_wday << STM32_RTC_DR_WDAY_SHIFT) & STM32_RTC_DR_WDAY);

	stm32_rtc_wpr_unlock(rtc);

	ret = stm32_rtc_enter_init_mode(rtc);
	if (ret) {
		dev_err(dev, "Can't enter in init mode. Set time aborted.\n");
		goto end;
	}

	writel_relaxed(tr, rtc->base + STM32_RTC_TR);
	writel_relaxed(dr, rtc->base + STM32_RTC_DR);

	stm32_rtc_exit_init_mode(rtc);

	ret = stm32_rtc_wait_sync(rtc);
end:
	stm32_rtc_wpr_lock(rtc);

	return ret;
}

static int stm32_rtc_read_alarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	struct stm32_rtc *rtc = dev_get_drvdata(dev);
	struct rtc_time *tm = &alrm->time;
	unsigned int alrmar, cr, isr;

	alrmar = readl_relaxed(rtc->base + STM32_RTC_ALRMAR);
	cr = readl_relaxed(rtc->base + STM32_RTC_CR);
	isr = readl_relaxed(rtc->base + STM32_RTC_ISR);

	if (alrmar & STM32_RTC_ALRMXR_DATE_MASK) {
		/*
		 * Date/day doesn't matter in Alarm comparison so alarm
		 * triggers every day
		 */
		tm->tm_mday = -1;
		tm->tm_wday = -1;
	} else {
		if (alrmar & STM32_RTC_ALRMXR_WDSEL) {
			/* Alarm is set to a day of week */
			tm->tm_mday = -1;
			tm->tm_wday = (alrmar & STM32_RTC_ALRMXR_WDAY) >>
				      STM32_RTC_ALRMXR_WDAY_SHIFT;
			tm->tm_wday %= 7;
		} else {
			/* Alarm is set to a day of month */
			tm->tm_wday = -1;
			tm->tm_mday = (alrmar & STM32_RTC_ALRMXR_DATE) >>
				       STM32_RTC_ALRMXR_DATE_SHIFT;
		}
	}

	if (alrmar & STM32_RTC_ALRMXR_HOUR_MASK) {
		/* Hours don't matter in Alarm comparison */
		tm->tm_hour = -1;
	} else {
		tm->tm_hour = (alrmar & STM32_RTC_ALRMXR_HOUR) >>
			       STM32_RTC_ALRMXR_HOUR_SHIFT;
		if (alrmar & STM32_RTC_ALRMXR_PM)
			tm->tm_hour += 12;
	}

	if (alrmar & STM32_RTC_ALRMXR_MIN_MASK) {
		/* Minutes don't matter in Alarm comparison */
		tm->tm_min = -1;
	} else {
		tm->tm_min = (alrmar & STM32_RTC_ALRMXR_MIN) >>
			      STM32_RTC_ALRMXR_MIN_SHIFT;
	}

	if (alrmar & STM32_RTC_ALRMXR_SEC_MASK) {
		/* Seconds don't matter in Alarm comparison */
		tm->tm_sec = -1;
	} else {
		tm->tm_sec = (alrmar & STM32_RTC_ALRMXR_SEC) >>
			      STM32_RTC_ALRMXR_SEC_SHIFT;
	}

	bcd2tm(tm);

	alrm->enabled = (cr & STM32_RTC_CR_ALRAE) ? 1 : 0;
	alrm->pending = (isr & STM32_RTC_ISR_ALRAF) ? 1 : 0;

	return 0;
}

static int stm32_rtc_alarm_irq_enable(struct device *dev, unsigned int enabled)
{
	struct stm32_rtc *rtc = dev_get_drvdata(dev);
	unsigned int isr, cr;

	cr = readl_relaxed(rtc->base + STM32_RTC_CR);

	stm32_rtc_wpr_unlock(rtc);

	/* We expose Alarm A to the kernel */
	if (enabled)
		cr |= (STM32_RTC_CR_ALRAIE | STM32_RTC_CR_ALRAE);
	else
		cr &= ~(STM32_RTC_CR_ALRAIE | STM32_RTC_CR_ALRAE);
	writel_relaxed(cr, rtc->base + STM32_RTC_CR);

	/* Clear event flag, otherwise new events won't be received */
	isr = readl_relaxed(rtc->base + STM32_RTC_ISR);
	isr &= ~STM32_RTC_ISR_ALRAF;
	writel_relaxed(isr, rtc->base + STM32_RTC_ISR);

	stm32_rtc_wpr_lock(rtc);

	return 0;
}

static int stm32_rtc_valid_alrm(struct stm32_rtc *rtc, struct rtc_time *tm)
{
	int cur_day, cur_mon, cur_year, cur_hour, cur_min, cur_sec;
	unsigned int dr = readl_relaxed(rtc->base + STM32_RTC_DR);
	unsigned int tr = readl_relaxed(rtc->base + STM32_RTC_TR);

	cur_day = (dr & STM32_RTC_DR_DATE) >> STM32_RTC_DR_DATE_SHIFT;
	cur_mon = (dr & STM32_RTC_DR_MONTH) >> STM32_RTC_DR_MONTH_SHIFT;
	cur_year = (dr & STM32_RTC_DR_YEAR) >> STM32_RTC_DR_YEAR_SHIFT;
	cur_sec = (tr & STM32_RTC_TR_SEC) >> STM32_RTC_TR_SEC_SHIFT;
	cur_min = (tr & STM32_RTC_TR_MIN) >> STM32_RTC_TR_MIN_SHIFT;
	cur_hour = (tr & STM32_RTC_TR_HOUR) >> STM32_RTC_TR_HOUR_SHIFT;

	/*
	 * Assuming current date is M-D-Y H:M:S.
	 * RTC alarm can't be set on a specific month and year.
	 * So the valid alarm range is:
	 *	M-D-Y H:M:S < alarm <= (M+1)-D-Y H:M:S
	 * with a specific case for December...
	 */
	if ((((tm->tm_year > cur_year) &&
	      (tm->tm_mon == 0x1) && (cur_mon == 0x12)) ||
	     ((tm->tm_year == cur_year) &&
	      (tm->tm_mon <= cur_mon + 1))) &&
	    ((tm->tm_mday > cur_day) ||
	     ((tm->tm_mday == cur_day) &&
	     ((tm->tm_hour > cur_hour) ||
	      ((tm->tm_hour == cur_hour) && (tm->tm_min > cur_min)) ||
	      ((tm->tm_hour == cur_hour) && (tm->tm_min == cur_min) &&
	       (tm->tm_sec >= cur_sec))))))
		return 0;

	return -EINVAL;
}

static int stm32_rtc_set_alarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	struct stm32_rtc *rtc = dev_get_drvdata(dev);
	struct rtc_time *tm = &alrm->time;
	unsigned int cr, isr, alrmar;
	int ret = 0;

	tm2bcd(tm);

	/*
	 * RTC alarm can't be set on a specific date, unless this date is
	 * up to the same day of month next month.
	 */
	if (stm32_rtc_valid_alrm(rtc, tm) < 0) {
		dev_err(dev, "Alarm can be set only on upcoming month.\n");
		return -EINVAL;
	}

	alrmar = 0;
	/* tm_year and tm_mon are not used because not supported by RTC */
	alrmar |= (tm->tm_mday << STM32_RTC_ALRMXR_DATE_SHIFT) &
		  STM32_RTC_ALRMXR_DATE;
	/* 24-hour format */
	alrmar &= ~STM32_RTC_ALRMXR_PM;
	alrmar |= (tm->tm_hour << STM32_RTC_ALRMXR_HOUR_SHIFT) &
		  STM32_RTC_ALRMXR_HOUR;
	alrmar |= (tm->tm_min << STM32_RTC_ALRMXR_MIN_SHIFT) &
		  STM32_RTC_ALRMXR_MIN;
	alrmar |= (tm->tm_sec << STM32_RTC_ALRMXR_SEC_SHIFT) &
		  STM32_RTC_ALRMXR_SEC;

	stm32_rtc_wpr_unlock(rtc);

	/* Disable Alarm */
	cr = readl_relaxed(rtc->base + STM32_RTC_CR);
	cr &= ~STM32_RTC_CR_ALRAE;
	writel_relaxed(cr, rtc->base + STM32_RTC_CR);

	/*
	 * Poll Alarm write flag to be sure that Alarm update is allowed: it
	 * takes around 2 rtc_ck clock cycles
	 */
	ret = readl_relaxed_poll_timeout_atomic(rtc->base + STM32_RTC_ISR,
						isr,
						(isr & STM32_RTC_ISR_ALRAWF),
						10, 100000);

	if (ret) {
		dev_err(dev, "Alarm update not allowed\n");
		goto end;
	}

	/* Write to Alarm register */
	writel_relaxed(alrmar, rtc->base + STM32_RTC_ALRMAR);

	if (alrm->enabled)
		stm32_rtc_alarm_irq_enable(dev, 1);
	else
		stm32_rtc_alarm_irq_enable(dev, 0);

end:
	stm32_rtc_wpr_lock(rtc);

	return ret;
}

static const struct rtc_class_ops stm32_rtc_ops = {
	.read_time	= stm32_rtc_read_time,
	.set_time	= stm32_rtc_set_time,
	.read_alarm	= stm32_rtc_read_alarm,
	.set_alarm	= stm32_rtc_set_alarm,
	.alarm_irq_enable = stm32_rtc_alarm_irq_enable,
};

static const struct stm32_rtc_data stm32_rtc_data = {
	.has_pclk = false,
};

static const struct stm32_rtc_data stm32h7_rtc_data = {
	.has_pclk = true,
};

static const struct of_device_id stm32_rtc_of_match[] = {
	{ .compatible = "st,stm32-rtc", .data = &stm32_rtc_data },
	{ .compatible = "st,stm32h7-rtc", .data = &stm32h7_rtc_data },
	{}
};
MODULE_DEVICE_TABLE(of, stm32_rtc_of_match);

static int stm32_rtc_init(struct platform_device *pdev,
			  struct stm32_rtc *rtc)
{
	unsigned int prer, pred_a, pred_s, pred_a_max, pred_s_max, cr;
	unsigned int rate;
	int ret = 0;

	rate = clk_get_rate(rtc->rtc_ck);

	/* Find prediv_a and prediv_s to obtain the 1Hz calendar clock */
	pred_a_max = STM32_RTC_PRER_PRED_A >> STM32_RTC_PRER_PRED_A_SHIFT;
	pred_s_max = STM32_RTC_PRER_PRED_S >> STM32_RTC_PRER_PRED_S_SHIFT;

	for (pred_a = pred_a_max; pred_a + 1 > 0; pred_a--) {
		pred_s = (rate / (pred_a + 1)) - 1;

		if (((pred_s + 1) * (pred_a + 1)) == rate)
			break;
	}

	/*
	 * Can't find a 1Hz, so give priority to RTC power consumption
	 * by choosing the higher possible value for prediv_a
	 */
	if ((pred_s > pred_s_max) || (pred_a > pred_a_max)) {
		pred_a = pred_a_max;
		pred_s = (rate / (pred_a + 1)) - 1;

		dev_warn(&pdev->dev, "rtc_ck is %s\n",
			 (rate < ((pred_a + 1) * (pred_s + 1))) ?
			 "fast" : "slow");
	}

	stm32_rtc_wpr_unlock(rtc);

	ret = stm32_rtc_enter_init_mode(rtc);
	if (ret) {
		dev_err(&pdev->dev,
			"Can't enter in init mode. Prescaler config failed.\n");
		goto end;
	}

	prer = (pred_s << STM32_RTC_PRER_PRED_S_SHIFT) & STM32_RTC_PRER_PRED_S;
	writel_relaxed(prer, rtc->base + STM32_RTC_PRER);
	prer |= (pred_a << STM32_RTC_PRER_PRED_A_SHIFT) & STM32_RTC_PRER_PRED_A;
	writel_relaxed(prer, rtc->base + STM32_RTC_PRER);

	/* Force 24h time format */
	cr = readl_relaxed(rtc->base + STM32_RTC_CR);
	cr &= ~STM32_RTC_CR_FMT;
	writel_relaxed(cr, rtc->base + STM32_RTC_CR);

	stm32_rtc_exit_init_mode(rtc);

	ret = stm32_rtc_wait_sync(rtc);
end:
	stm32_rtc_wpr_lock(rtc);

	return ret;
}

static int stm32_rtc_probe(struct platform_device *pdev)
{
	struct stm32_rtc *rtc;
	struct resource *res;
	const struct of_device_id *match;
	int ret;

	rtc = devm_kzalloc(&pdev->dev, sizeof(*rtc), GFP_KERNEL);
	if (!rtc)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	rtc->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(rtc->base))
		return PTR_ERR(rtc->base);

	rtc->dbp = syscon_regmap_lookup_by_phandle(pdev->dev.of_node,
						   "st,syscfg");
	if (IS_ERR(rtc->dbp)) {
		dev_err(&pdev->dev, "no st,syscfg\n");
		return PTR_ERR(rtc->dbp);
	}

	match = of_match_device(stm32_rtc_of_match, &pdev->dev);
	rtc->data = (struct stm32_rtc_data *)match->data;

	if (!rtc->data->has_pclk) {
		rtc->pclk = NULL;
		rtc->rtc_ck = devm_clk_get(&pdev->dev, NULL);
	} else {
		rtc->pclk = devm_clk_get(&pdev->dev, "pclk");
		if (IS_ERR(rtc->pclk)) {
			dev_err(&pdev->dev, "no pclk clock");
			return PTR_ERR(rtc->pclk);
		}
		rtc->rtc_ck = devm_clk_get(&pdev->dev, "rtc_ck");
	}
	if (IS_ERR(rtc->rtc_ck)) {
		dev_err(&pdev->dev, "no rtc_ck clock");
		return PTR_ERR(rtc->rtc_ck);
	}

	if (rtc->data->has_pclk) {
		ret = clk_prepare_enable(rtc->pclk);
		if (ret)
			return ret;
	}

	ret = clk_prepare_enable(rtc->rtc_ck);
	if (ret)
		goto err;

	regmap_update_bits(rtc->dbp, PWR_CR, PWR_CR_DBP, PWR_CR_DBP);

	/*
	 * After a system reset, RTC_ISR.INITS flag can be read to check if
	 * the calendar has been initalized or not. INITS flag is reset by a
	 * power-on reset (no vbat, no power-supply). It is not reset if
	 * rtc_ck parent clock has changed (so RTC prescalers need to be
	 * changed). That's why we cannot rely on this flag to know if RTC
	 * init has to be done.
	 */
	ret = stm32_rtc_init(pdev, rtc);
	if (ret)
		goto err;

	rtc->irq_alarm = platform_get_irq(pdev, 0);
	if (rtc->irq_alarm <= 0) {
		dev_err(&pdev->dev, "no alarm irq\n");
		ret = rtc->irq_alarm;
		goto err;
	}

	platform_set_drvdata(pdev, rtc);

	ret = device_init_wakeup(&pdev->dev, true);
	if (ret)
		dev_warn(&pdev->dev,
			 "alarm won't be able to wake up the system");

	rtc->rtc_dev = devm_rtc_device_register(&pdev->dev, pdev->name,
			&stm32_rtc_ops, THIS_MODULE);
	if (IS_ERR(rtc->rtc_dev)) {
		ret = PTR_ERR(rtc->rtc_dev);
		dev_err(&pdev->dev, "rtc device registration failed, err=%d\n",
			ret);
		goto err;
	}

	/* Handle RTC alarm interrupts */
	ret = devm_request_threaded_irq(&pdev->dev, rtc->irq_alarm, NULL,
					stm32_rtc_alarm_irq,
					IRQF_TRIGGER_RISING | IRQF_ONESHOT,
					pdev->name, rtc);
	if (ret) {
		dev_err(&pdev->dev, "IRQ%d (alarm interrupt) already claimed\n",
			rtc->irq_alarm);
		goto err;
	}

	/*
	 * If INITS flag is reset (calendar year field set to 0x00), calendar
	 * must be initialized
	 */
	if (!(readl_relaxed(rtc->base + STM32_RTC_ISR) & STM32_RTC_ISR_INITS))
		dev_warn(&pdev->dev, "Date/Time must be initialized\n");

	return 0;
err:
	if (rtc->data->has_pclk)
		clk_disable_unprepare(rtc->pclk);
	clk_disable_unprepare(rtc->rtc_ck);

	regmap_update_bits(rtc->dbp, PWR_CR, PWR_CR_DBP, 0);

	device_init_wakeup(&pdev->dev, false);

	return ret;
}

static int stm32_rtc_remove(struct platform_device *pdev)
{
	struct stm32_rtc *rtc = platform_get_drvdata(pdev);
	unsigned int cr;

	/* Disable interrupts */
	stm32_rtc_wpr_unlock(rtc);
	cr = readl_relaxed(rtc->base + STM32_RTC_CR);
	cr &= ~STM32_RTC_CR_ALRAIE;
	writel_relaxed(cr, rtc->base + STM32_RTC_CR);
	stm32_rtc_wpr_lock(rtc);

	clk_disable_unprepare(rtc->rtc_ck);
	if (rtc->data->has_pclk)
		clk_disable_unprepare(rtc->pclk);

	/* Enable backup domain write protection */
	regmap_update_bits(rtc->dbp, PWR_CR, PWR_CR_DBP, 0);

	device_init_wakeup(&pdev->dev, false);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int stm32_rtc_suspend(struct device *dev)
{
	struct stm32_rtc *rtc = dev_get_drvdata(dev);

	if (rtc->data->has_pclk)
		clk_disable_unprepare(rtc->pclk);

	if (device_may_wakeup(dev))
		return enable_irq_wake(rtc->irq_alarm);

	return 0;
}

static int stm32_rtc_resume(struct device *dev)
{
	struct stm32_rtc *rtc = dev_get_drvdata(dev);
	int ret = 0;

	if (rtc->data->has_pclk) {
		ret = clk_prepare_enable(rtc->pclk);
		if (ret)
			return ret;
	}

	ret = stm32_rtc_wait_sync(rtc);
	if (ret < 0)
		return ret;

	if (device_may_wakeup(dev))
		return disable_irq_wake(rtc->irq_alarm);

	return ret;
}
#endif

static SIMPLE_DEV_PM_OPS(stm32_rtc_pm_ops,
			 stm32_rtc_suspend, stm32_rtc_resume);

static struct platform_driver stm32_rtc_driver = {
	.probe		= stm32_rtc_probe,
	.remove		= stm32_rtc_remove,
	.driver		= {
		.name	= DRIVER_NAME,
		.pm	= &stm32_rtc_pm_ops,
		.of_match_table = stm32_rtc_of_match,
	},
};

module_platform_driver(stm32_rtc_driver);

MODULE_ALIAS("platform:" DRIVER_NAME);
MODULE_AUTHOR("Amelie Delaunay <amelie.delaunay@st.com>");
MODULE_DESCRIPTION("STMicroelectronics STM32 Real Time Clock driver");
MODULE_LICENSE("GPL v2");
