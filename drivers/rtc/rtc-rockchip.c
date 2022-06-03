// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2022 Rockchip Electronics Co., Ltd
 */
#include <linux/bcd.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/mfd/syscon.h>
#include <linux/clk.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/regmap.h>
#include <linux/rtc.h>

/* RTC_CTRL_REG bitfields */

#define RTC_REG(x)			((x))
#define RTC_SET_SECONDS			RTC_REG(0x0)
#define RTC_SET_MINUTES			RTC_REG(0x4)
#define RTC_SET_HOURS			RTC_REG(0x8)
#define RTC_SET_DAYS			RTC_REG(0xc)
#define RTC_SET_MONTHS			RTC_REG(0x10)
#define RTC_SET_YEARL			RTC_REG(0x14)
#define RTC_SET_YEARH			RTC_REG(0x18)
#define RTC_SET_WEEKS			RTC_REG(0x1c)
#define RTC_ALARM_SECONDS		RTC_REG(0x20)
#define RTC_ALARM_MINUTES		RTC_REG(0x24)
#define RTC_ALARM_HOURS			RTC_REG(0x28)
#define RTC_ALARM_DAYS			RTC_REG(0x2c)
#define RTC_ALARM_MONTHS		RTC_REG(0x30)
#define RTC_ALARM_YEARL			RTC_REG(0x34)
#define RTC_ALARM_YEARH			RTC_REG(0x38)
#define RTC_CTRL			RTC_REG(0x3C)
#define RTC_STATUS0			RTC_REG(0x40)
#define RTC_STATUS1			RTC_REG(0x44)
#define RTC_INT0_EN			RTC_REG(0x48)
#define RTC_INT1_EN			RTC_REG(0x4c)
#define RTC_MSEC_CTRL			RTC_REG(0x50)
#define RTC_MSEC_CNT			RTC_REG(0x54)
#define RTC_COMP_H			RTC_REG(0x58)
#define RTC_COMP_D			RTC_REG(0x5c)
#define RTC_COMP_M			RTC_REG(0x60)
#define RTC_ANALOG_CTRL			RTC_REG(0x64)
#define RTC_ANALOG_TEST			RTC_REG(0x68)
#define RTC_LDO_CTRL			RTC_REG(0x6c)
#define RTC_XO_TRIM0			RTC_REG(0x70)
#define RTC_XO_TRIM1			RTC_REG(0x74)
#define RTC_VPTAT_TRIM			RTC_REG(0x78)
#define RTC_ANALOG_EN			RTC_REG(0x7c)
#define RTC_CLK32K_TEST			RTC_REG(0x80)
#define RTC_TEST_ST			RTC_REG(0x84)
#define RTC_TEST_LEN			RTC_REG(0x88)
#define RTC_CNT_0			RTC_REG(0x8c)
#define RTC_CNT_1			RTC_REG(0x90)
#define RTC_CNT_2			RTC_REG(0x94)
#define RTC_CNT_3			RTC_REG(0x98)
#define RTC_MAX_REGISTER		RTC_CNT_3

#define VI_GRF_VI_MISC_CON0		0x50000
#define RTC_CLAMP_EN		BIT(6)

/* RTC_CTRL_REG bitfields */
#define RTC_CTRL_REG_START_RTC		BIT(0)

/* RK630 has a shadowed register for saving a "frozen" RTC time.
 * When user setting "GET_TIME" to 1, the time will save in this shadowed
 * register. If set "READSEL" to 1, user read rtc time register, actually
 * get the time of that moment. If we need the real time, clr this bit.
 */
#define RTC_CTRL_REG_RTC_GET_TIME	BIT(6)
#define RTC_CTRL_REG_RTC_READSEL_M	BIT(7)
#define RTC_INT_REG_ALARM_EN		BIT(7)
#define RTC_D2A_XO_EN			BIT(0)
#define RTC_D2A_CLK_OUT_EN		BIT(5)

#define RTC_STATUS_MASK			0xFF

#define SECONDS_REG_MSK			0x7F
#define MINUTES_REG_MAK			0x7F
#define HOURS_REG_MSK			0x3F
#define DAYS_REG_MSK			0x3F
#define MONTHS_REG_MSK			0x1F
#define YEARS_REG_MSK			0xFF
#define WEEKS_REG_MSK			0x7

#define RTC_VREF_INIT			0x40

#define D2A_POR_REG_SEL1		BIT(4)
#define D2A_POR_REG_SEL0		BIT(1)

#define NUM_TIME_REGS			8
#define NUM_ALARM_REGS			7

#define DISABLE_ALARM_INT		0x3F
#define ENABLE_ALARM_INT		0xFF
#define ALARM_INT_STATUS		BIT(4)

#define CLK32K_TEST_EN			BIT(0)
#define CLK32K_TEST_START		BIT(0)
#define CLK32K_TEST_STATUS		BIT(1)
#define CLK32K_TEST_DONE		BIT(2)
#define CLK32K_TEST_LEN			2

#define CLK32K_COMP_DIR_ADD		BIT(7)
#define CLK32K_COMP_EN			BIT(2)
#define CLK32K_NO_COMP			0x1

#define CLK32K_TEST_REF_CLK		24000000

#define RTC_WRITE_MASK			0xc4522900

enum {
	ROCKCHIP_RV1106_RTC = 1,
};

struct rockchip_rtc {
	struct regmap *regmap;
	struct rtc_device *rtc;
	struct regmap *grf;
	struct clk_bulk_data *clks;
	int num_clks;
	int irq;
	unsigned int flag;
	unsigned int mode;
};

static unsigned int rockchip_rtc_write(struct regmap *map,
				       u32 offset, u32 val)
{
	return regmap_write(map, offset, val | RTC_WRITE_MASK);
}

static unsigned int rockchip_rtc_update_bits(struct regmap *map,
					     u32 offset, u32 mask,
					     u32 set)
{
	unsigned int val;

	regmap_read(map, offset, &val);
	return regmap_write(map, offset, (val & ~mask) | set | RTC_WRITE_MASK);
}

/* Read current time and date in RTC */
static int rockchip_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
	struct rockchip_rtc *rtc = dev_get_drvdata(dev);
	u32 rtc_data[NUM_TIME_REGS];
	int ret;
	int yearl, yearh;

	/* No shadowed registers, need read time three time to update time */
	ret = regmap_bulk_read(rtc->regmap, RTC_SET_SECONDS,
			       rtc_data, NUM_TIME_REGS);
	if (ret) {
		dev_err(dev, "Failed to bulk read rtc_data: %d\n", ret);
		return ret;
	}
	ret = regmap_bulk_read(rtc->regmap, RTC_SET_SECONDS,
			       rtc_data, NUM_TIME_REGS);
	if (ret) {
		dev_err(dev, "Failed to bulk read rtc_data: %d\n", ret);
		return ret;
	}
	ret = regmap_bulk_read(rtc->regmap, RTC_SET_SECONDS,
			       rtc_data, NUM_TIME_REGS);
	if (ret) {
		dev_err(dev, "Failed to bulk read rtc_data: %d\n", ret);
		return ret;
	}

	tm->tm_sec = bcd2bin(rtc_data[0] & SECONDS_REG_MSK);
	tm->tm_min = bcd2bin(rtc_data[1] & MINUTES_REG_MAK);
	tm->tm_hour = bcd2bin(rtc_data[2] & HOURS_REG_MSK);
	tm->tm_mday = bcd2bin(rtc_data[3] & DAYS_REG_MSK);
	tm->tm_mon = (bcd2bin(rtc_data[4] & MONTHS_REG_MSK)) - 1;
	yearl = (bcd2bin(rtc_data[5] & YEARS_REG_MSK));
	yearh = (bcd2bin(rtc_data[6] & YEARS_REG_MSK));
	tm->tm_year = yearh * 100 + yearl + 100;
	tm->tm_wday = bcd2bin(rtc_data[7] & WEEKS_REG_MSK);

	dev_dbg(dev, "RTC date/time %4d-%02d-%02d(%d) %02d:%02d:%02d\n",
		1900 + tm->tm_year, tm->tm_mon + 1, tm->tm_mday,
		tm->tm_wday, tm->tm_hour, tm->tm_min, tm->tm_sec);

	return ret;
}

/* Set current time and date in RTC */
static int rockchip_rtc_set_time(struct device *dev, struct rtc_time *tm)
{
	struct rockchip_rtc *rtc = dev_get_drvdata(dev);
	u32 rtc_data[NUM_TIME_REGS];
	int ret, status = 0;
	int yearl, yearh;

	dev_dbg(dev, "set RTC date/time %4d-%02d-%02d(%d) %02d:%02d:%02d\n",
		1900 + tm->tm_year, tm->tm_mon + 1, tm->tm_mday,
		tm->tm_wday, tm->tm_hour, tm->tm_min, tm->tm_sec);

	rtc_data[0] = bin2bcd(tm->tm_sec) | RTC_WRITE_MASK;
	rtc_data[1] = bin2bcd(tm->tm_min) | RTC_WRITE_MASK;
	rtc_data[2] = bin2bcd(tm->tm_hour) | RTC_WRITE_MASK;
	rtc_data[3] = bin2bcd(tm->tm_mday) | RTC_WRITE_MASK;
	rtc_data[4] = bin2bcd(tm->tm_mon + 1) | RTC_WRITE_MASK;
	if (tm->tm_year > 199) {
		yearh = (tm->tm_year - 100) / 100;
		yearl = tm->tm_year - 100 - yearh * 100;
	} else {
		yearh = 0;
		yearl = tm->tm_year - 100 - yearh * 100;
	}
	rtc_data[5] = bin2bcd(yearl) | RTC_WRITE_MASK;
	rtc_data[6] = bin2bcd(yearh) | RTC_WRITE_MASK;
	rtc_data[7] = bin2bcd(tm->tm_wday) | RTC_WRITE_MASK;

	/* Stop RTC while updating the RTC registers */
	ret = rockchip_rtc_update_bits(rtc->regmap, RTC_CTRL,
				       RTC_CTRL_REG_START_RTC, 0);
	if (ret) {
		dev_err(dev, "Failed to update RTC control: %d\n", ret);
		return ret;
	}
	ret = regmap_read(rtc->regmap, RTC_STATUS1, &status);
	while (status & RTC_CTRL_REG_START_RTC) {
		udelay(1);
		ret = regmap_read(rtc->regmap, RTC_STATUS1, &status);
		if (ret) {
			dev_err(dev, "Failed to read RTC_STATUS1: %d\n", ret);
			return ret;
		}
	}

	ret = regmap_bulk_write(rtc->regmap, RTC_SET_SECONDS,
				rtc_data, NUM_TIME_REGS);
	if (ret) {
		dev_err(dev, "Failed to bull write rtc_data: %d\n", ret);
		return ret;
	}

	/* Start RTC again */
	ret = rockchip_rtc_update_bits(rtc->regmap, RTC_CTRL,
				       RTC_CTRL_REG_RTC_READSEL_M |
				       RTC_CTRL_REG_START_RTC,
				       RTC_CTRL_REG_RTC_READSEL_M |
				       RTC_CTRL_REG_START_RTC);
	if (ret) {
		dev_err(dev, "Failed to update RTC control: %d\n", ret);
		return ret;
	}
	ret = regmap_read(rtc->regmap, RTC_STATUS1, &status);
	while (!(status & RTC_CTRL_REG_START_RTC)) {
		udelay(1);
		ret = regmap_read(rtc->regmap, RTC_STATUS1, &status);
		if (ret) {
			dev_err(dev, "Failed to read RTC_STATUS1: %d\n", ret);
			return ret;
		}
	}
	return 0;
}

/* Read alarm time and date in RTC */
static int rockchip_rtc_readalarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	struct rockchip_rtc *rtc = dev_get_drvdata(dev);
	u32 alrm_data[NUM_ALARM_REGS];
	u32 int_reg;
	int yearl, yearh;
	int ret;

	ret = regmap_bulk_read(rtc->regmap,
			       RTC_ALARM_SECONDS,
			       alrm_data, NUM_ALARM_REGS);
	if (ret) {
		dev_err(dev, "Failed to read RTC alarm date REG: %d\n", ret);
		return ret;
	}

	alrm->time.tm_sec = bcd2bin(alrm_data[0] & SECONDS_REG_MSK);
	alrm->time.tm_min = bcd2bin(alrm_data[1] & MINUTES_REG_MAK);
	alrm->time.tm_hour = bcd2bin(alrm_data[2] & HOURS_REG_MSK);
	alrm->time.tm_mday = bcd2bin(alrm_data[3] & DAYS_REG_MSK);
	alrm->time.tm_mon = (bcd2bin(alrm_data[4] & MONTHS_REG_MSK)) - 1;
	yearl = (bcd2bin(alrm_data[5] & YEARS_REG_MSK));
	yearh = (bcd2bin(alrm_data[6] & YEARS_REG_MSK));
	alrm->time.tm_year = yearh * 100 + yearl + 100;

	ret = regmap_read(rtc->regmap, RTC_INT0_EN, &int_reg);
	if (ret) {
		dev_err(dev, "Failed to read RTC INT REG: %d\n", ret);
		return ret;
	}

	dev_dbg(dev,
		"alrm read RTC date/time %4d-%02d-%02d(%d) %02d:%02d:%02d\n",
		1900 + alrm->time.tm_year, alrm->time.tm_mon + 1,
		alrm->time.tm_mday, alrm->time.tm_wday, alrm->time.tm_hour,
		alrm->time.tm_min, alrm->time.tm_sec);

	alrm->enabled = (int_reg & RTC_INT_REG_ALARM_EN) ? 1 : 0;

	return 0;
}

static int rockchip_rtc_stop_alarm(struct rockchip_rtc *rtc)
{
	int ret;

	ret = rockchip_rtc_write(rtc->regmap, RTC_INT0_EN, 0);

	return ret;
}

static int rockchip_rtc_start_alarm(struct rockchip_rtc *rtc)
{
	int ret;

	ret = rockchip_rtc_write(rtc->regmap, RTC_STATUS0, RTC_STATUS_MASK);
	ret = rockchip_rtc_write(rtc->regmap, RTC_STATUS0, 0);
	ret = rockchip_rtc_write(rtc->regmap, RTC_INT0_EN, ENABLE_ALARM_INT);

	return ret;
}

static int rockchip_rtc_setalarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	struct rockchip_rtc *rtc = dev_get_drvdata(dev);
	u32 alrm_data[NUM_ALARM_REGS];
	int yearl, yearh;
	int ret;

	ret = rockchip_rtc_stop_alarm(rtc);
	if (ret) {
		dev_err(dev, "Failed to stop alarm: %d\n", ret);
		return ret;
	}
	dev_dbg(dev,
		"alrm set RTC date/time %4d-%02d-%02d(%d) %02d:%02d:%02d\n",
		1900 + alrm->time.tm_year, alrm->time.tm_mon + 1,
		alrm->time.tm_mday, alrm->time.tm_wday, alrm->time.tm_hour,
		alrm->time.tm_min, alrm->time.tm_sec);

	alrm_data[0] = bin2bcd(alrm->time.tm_sec) | RTC_WRITE_MASK;
	alrm_data[1] = bin2bcd(alrm->time.tm_min) | RTC_WRITE_MASK;
	alrm_data[2] = bin2bcd(alrm->time.tm_hour) | RTC_WRITE_MASK;
	alrm_data[3] = bin2bcd(alrm->time.tm_mday) | RTC_WRITE_MASK;
	alrm_data[4] = bin2bcd(alrm->time.tm_mon + 1) | RTC_WRITE_MASK;
	if (alrm->time.tm_year > 199) {
		yearh = (alrm->time.tm_year - 100) / 100;
		yearl = alrm->time.tm_year - 100 - yearh * 100;
	} else {
		yearh = 0;
		yearl = alrm->time.tm_year - 100 - yearh * 100;
	}
	alrm_data[5] = bin2bcd(yearl) | RTC_WRITE_MASK;
	alrm_data[6] = bin2bcd(yearh) | RTC_WRITE_MASK;

	ret = regmap_bulk_write(rtc->regmap,
				RTC_ALARM_SECONDS,
				alrm_data, NUM_ALARM_REGS);
	if (ret) {
		dev_err(dev, "Failed to bulk write: %d\n", ret);
		return ret;
	}
	if (alrm->enabled) {
		ret = rockchip_rtc_start_alarm(rtc);
		if (ret) {
			dev_err(dev, "Failed to start alarm: %d\n", ret);
			return ret;
		}
	}
	return 0;
}

static int rockchip_rtc_alarm_irq_enable(struct device *dev,
					 unsigned int enabled)
{
	struct rockchip_rtc *rtc = dev_get_drvdata(dev);

	if (enabled)
		return rockchip_rtc_start_alarm(rtc);

	return rockchip_rtc_stop_alarm(rtc);
}

/*
 * We will just handle setting the frequency and make use the framework for
 * reading the periodic interrupts.
 *
 */
static irqreturn_t rockchip_rtc_alarm_irq(int irq, void *data)
{
	struct rockchip_rtc *rtc = data;
	int ret, status;

	ret = regmap_read(rtc->regmap, RTC_STATUS0, &status);
	if (ret) {
		pr_err("Failed to read RTC INT REG: %d\n", ret);
		return ret;
	}

	ret = rockchip_rtc_write(rtc->regmap, RTC_STATUS0, status);
	if (ret) {
		pr_err("%s:Failed to update RTC status: %d\n", __func__, ret);
		return ret;
	}
	if (status & ALARM_INT_STATUS) {
		pr_info("Alarm by: %s\n", __func__);
		rtc_update_irq(rtc->rtc, 1, RTC_IRQF | RTC_AF);
	}

	return IRQ_HANDLED;
}

static const struct rtc_class_ops rockchip_rtc_ops = {
	.read_time = rockchip_rtc_read_time,
	.set_time = rockchip_rtc_set_time,
	.read_alarm = rockchip_rtc_readalarm,
	.set_alarm = rockchip_rtc_setalarm,
	.alarm_irq_enable = rockchip_rtc_alarm_irq_enable,
};

/*
 * Due to the analog generator 32k clock affected by
 * temperature, voltage, clock precision need test
 * with the environment change. In rtc test,
 * use 24M clock as reference clock to measure the 32k clock.
 * Before start test 32k clock, we should enable clk32k test(0x80),
 * and configure test length, when rtc test done(0x84[2]),
 * latch the 24M clock domain counter,
 * and read out the counter from rtc_test
 * registers(0x8c~0x98) via apb bus.
 * In RTC digital design, we set three level compensation,
 * the compensation value due to the
 * RTC 32k clock test result, and if we need compensation,
 * we need configure the compensation enable bit.
 * Comp every hour, compensation at last minute every hour,
 * and support add time and sub time by the MSB bit.
 * Comp every day, compensation at last minute in last hour every day,
 * and support add time and sub time by the MSB bit.
 * Comp every month, compensation at last minute
 * in last hour in last day every month,
 * and support add time and sub time by the MSB bit.
 */
static int rockchip_rtc_compensation(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct rockchip_rtc *rtc = dev_get_drvdata(&pdev->dev);
	u64 camp;
	u32 count[4], counts, g_ref, tcamp;
	int ret, done = 0, trim_dir, c_hour,
	    c_day, c_det_day, c_mon, c_det_mon;

	ret = rockchip_rtc_update_bits(rtc->regmap, RTC_CLK32K_TEST,
				       CLK32K_TEST_EN, CLK32K_TEST_EN);
	if (ret) {
		dev_err(dev,
			"%s:Failed to update RTC CLK32K TEST: %d\n",
			__func__, ret);
		return ret;
	}

	ret = rockchip_rtc_write(rtc->regmap, RTC_TEST_LEN,
				 CLK32K_TEST_LEN);
	if (ret) {
		dev_err(dev,
			"%s:Failed to update RTC CLK32K TEST LEN: %d\n",
			__func__, ret);
		return ret;
	}

	ret = rockchip_rtc_update_bits(rtc->regmap, RTC_TEST_ST,
				       CLK32K_TEST_START,
				       CLK32K_TEST_START);
	if (ret) {
		dev_err(dev,
			"%s:Failed to update RTC CLK32K TEST STATUS : %d\n",
			__func__, ret);
		return ret;
	}

	ret = regmap_read_poll_timeout(rtc->regmap, RTC_TEST_ST, done,
				       (done & CLK32K_TEST_DONE), 0, 1000);
	if (ret)
		dev_err(dev,
			"%s:timeout waiting for RTC TEST STATUS : %d\n",
			__func__, ret);

	ret = regmap_bulk_read(rtc->regmap,
			       RTC_CNT_0,
			       count, 4);
	if (ret) {
		dev_err(dev, "Failed to read RTC count REG: %d\n", ret);
		return ret;
	}

	counts = count[0] | (count[1] << 8) |
		 (count[2] << 16) | (count[3] << 24);
	g_ref = CLK32K_TEST_REF_CLK * (CLK32K_TEST_LEN + 1);

	if (counts > g_ref) {
		trim_dir = 0;
		camp = 36ULL * (32768 * (counts - g_ref));
		do_div(camp, (g_ref / 100));
	} else {
		trim_dir = CLK32K_COMP_DIR_ADD;
		camp = 36ULL * (32768 * (g_ref - counts));
		do_div(camp, (g_ref / 100));
	}
	tcamp = (u32)camp;
	c_hour = DIV_ROUND_CLOSEST(tcamp, 32768);
	c_day = DIV_ROUND_CLOSEST(24 * tcamp, 32768);
	c_mon = DIV_ROUND_CLOSEST(30 * 24 * tcamp, 32768);

	if (c_hour > 1)
		rockchip_rtc_write(rtc->regmap, RTC_COMP_H, (c_hour - 1) | trim_dir);
	else
		rockchip_rtc_write(rtc->regmap, RTC_COMP_H, CLK32K_NO_COMP);

	if (c_day > c_hour * 23) {
		c_det_day = c_day - c_hour * 23;
		trim_dir = CLK32K_COMP_DIR_ADD;
	} else {
		c_det_day = c_hour * 24 - c_day;
		trim_dir = 0;
	}

	if (c_det_day > 1)
		rockchip_rtc_write(rtc->regmap, RTC_COMP_D,
				   (c_det_day - 1) | trim_dir);
	else
		rockchip_rtc_write(rtc->regmap, RTC_COMP_D, CLK32K_NO_COMP);

	if (c_mon > (29 * c_day + 23 * c_hour)) {
		c_det_mon = c_mon - 29 * c_day - 23 * c_hour;
		trim_dir = CLK32K_COMP_DIR_ADD;
	} else {
		c_det_mon = 29 * c_day + 23 * c_hour - c_mon;
		trim_dir = 0;
	}

	if (c_det_mon)
		rockchip_rtc_write(rtc->regmap, RTC_COMP_M,
				   (c_det_mon - 1) | trim_dir);
	else
		rockchip_rtc_write(rtc->regmap, RTC_COMP_M, CLK32K_NO_COMP);

	ret = regmap_read(rtc->regmap, RTC_CTRL, &done);
	if (ret) {
		dev_err(dev, "Failed to read RTC_CTRL: %d\n", ret);
		return ret;
	}

	ret = rockchip_rtc_update_bits(rtc->regmap, RTC_CTRL,
				       CLK32K_COMP_EN, CLK32K_COMP_EN);
	if (ret) {
		dev_err(dev,
			"%s:Failed to update RTC CTRL : %d\n", __func__, ret);
		return ret;
	}
	return 0;
}

/* Enable the alarm if it should be enabled (in case it was disabled to
 * prevent use as a wake source).
 */
#ifdef CONFIG_PM_SLEEP
/* Turn off the alarm if it should not be a wake source. */
static int rockchip_rtc_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct rockchip_rtc *rtc = dev_get_drvdata(&pdev->dev);

	if (device_may_wakeup(dev))
		enable_irq_wake(rtc->irq);

	if (rtc->grf) {
		switch (rtc->mode) {
		case ROCKCHIP_RV1106_RTC:
			regmap_write(rtc->grf, VI_GRF_VI_MISC_CON0,
				     (RTC_CLAMP_EN << 16));
			break;
		default:
			return -EINVAL;
		}
	}
	clk_bulk_disable_unprepare(rtc->num_clks, rtc->clks);

	return 0;
}

/* Enable the alarm if it should be enabled (in case it was disabled to
 * prevent use as a wake source).
 */
static int rockchip_rtc_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct rockchip_rtc *rtc = dev_get_drvdata(&pdev->dev);
	int ret;

	if (device_may_wakeup(dev))
		disable_irq_wake(rtc->irq);

	if (rtc->grf) {
		switch (rtc->mode) {
		case ROCKCHIP_RV1106_RTC:
			regmap_write(rtc->grf, VI_GRF_VI_MISC_CON0,
				     (RTC_CLAMP_EN << 16) | RTC_CLAMP_EN);
			break;
		default:
			return -EINVAL;
		}
	}
	ret = clk_bulk_prepare_enable(rtc->num_clks, rtc->clks);
	if (ret) {
		dev_err(dev, "Cannot enable clock.\n");
		return ret;
	}

	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(rockchip_rtc_pm_ops,
	rockchip_rtc_suspend, rockchip_rtc_resume);

static const struct of_device_id rockchip_rtc_of_match[] = {
	{
		.compatible = "rockchip,rv1106-rtc",
		.data = (void *)ROCKCHIP_RV1106_RTC
	},
	{},
};
MODULE_DEVICE_TABLE(of, rockchip_rtc_of_match);

static void rockchip_rtc_clk_disable(void *data)
{
	struct rockchip_rtc *rtc = data;

	clk_bulk_disable_unprepare(rtc->num_clks, rtc->clks);
}

static int rockchip_rtc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct rockchip_rtc *rtc;
	int ret;
	struct rtc_time tm_read, tm = {
		.tm_wday = 0,
		.tm_year = 121,
		.tm_mon = 0,
		.tm_mday = 1,
		.tm_hour = 12,
		.tm_min = 0,
		.tm_sec = 0,
	};

	rtc = devm_kzalloc(&pdev->dev, sizeof(*rtc), GFP_KERNEL);
	if (!rtc)
		return -ENOMEM;

	rtc->regmap = device_node_to_regmap(np);
	if (IS_ERR(rtc->regmap))
		return dev_err_probe(dev, PTR_ERR(rtc->regmap),
				     "no regmap available\n");

	rtc->mode = (unsigned int)of_device_get_match_data(&pdev->dev);
	rtc->grf = syscon_regmap_lookup_by_phandle(np, "rockchip,grf");
	if (IS_ERR(rtc->grf)) {
		dev_warn(dev, "Missing rockchip,grf property\n");
		rtc->grf = NULL;
	} else {
		switch (rtc->mode) {
		case ROCKCHIP_RV1106_RTC:
			regmap_write(rtc->grf, VI_GRF_VI_MISC_CON0,
				     (RTC_CLAMP_EN << 16) | RTC_CLAMP_EN);
			break;
		default:
			return -EINVAL;
		}
	}

	platform_set_drvdata(pdev, rtc);

	rtc->num_clks = devm_clk_bulk_get_all(&pdev->dev, &rtc->clks);
	if (rtc->num_clks < 1)
		return -ENODEV;
	ret = clk_bulk_prepare_enable(rtc->num_clks, rtc->clks);
	if (ret)
		return dev_err_probe(dev, ret, "Cannot enable clock.\n");
	ret = devm_add_action_or_reset(dev, rockchip_rtc_clk_disable, rtc);
	if (ret)
		return dev_err_probe(dev, ret,
				     "Failed to add clk disable action.");

	ret = rockchip_rtc_update_bits(rtc->regmap, RTC_VPTAT_TRIM,
				       D2A_POR_REG_SEL1,
				       D2A_POR_REG_SEL1);
	if (ret)
		return dev_err_probe(&pdev->dev, ret,
				     "Failed to write RTC_VPTAT_TRIM\n");
	ret = rockchip_rtc_update_bits(rtc->regmap, RTC_ANALOG_EN,
				       D2A_POR_REG_SEL0,
				       0x00);
	if (ret)
		return dev_err_probe(&pdev->dev, ret,
				     "Failed to write RTC_ANALOG_EN\n");

	ret = rockchip_rtc_update_bits(rtc->regmap, RTC_LDO_CTRL,
				       RTC_D2A_XO_EN,
				       RTC_D2A_XO_EN);
	if (ret)
		return dev_err_probe(&pdev->dev, ret,
				     "Failed to update RTC_LDO_CTRL\n");

	ret = rockchip_rtc_update_bits(rtc->regmap, RTC_ANALOG_EN,
				       RTC_D2A_CLK_OUT_EN,
				       RTC_D2A_CLK_OUT_EN);
	if (ret)
		return dev_err_probe(&pdev->dev, ret,
				     "Failed to update RTC_ANALOG_EN\n");

	rockchip_rtc_compensation(&pdev->dev);

	/* start rtc running by default, and use shadowed timer. */
	ret = rockchip_rtc_update_bits(rtc->regmap, RTC_CTRL,
				       RTC_CTRL_REG_START_RTC |
				       RTC_CTRL_REG_RTC_READSEL_M,
				       RTC_CTRL_REG_RTC_READSEL_M |
				       RTC_CTRL_REG_START_RTC);
	if (ret)
		return dev_err_probe(&pdev->dev, ret,
				     "Failed to update RTC control\n");

	ret = rockchip_rtc_write(rtc->regmap, RTC_STATUS0, RTC_STATUS_MASK);
	if (ret)
		return dev_err_probe(&pdev->dev, ret,
				     "Failed to write RTC status0\n");

	ret = rockchip_rtc_write(rtc->regmap, RTC_STATUS0, 0);
	if (ret)
		return dev_err_probe(&pdev->dev, ret,
				     "Failed to write RTC status0\n");

	device_init_wakeup(&pdev->dev, 1);

	rockchip_rtc_read_time(&pdev->dev, &tm_read);
	if (rtc_valid_tm(&tm_read) != 0)
		rockchip_rtc_set_time(&pdev->dev, &tm);

	rtc->rtc = devm_rtc_allocate_device(&pdev->dev);
	if (IS_ERR(rtc->rtc))
		return PTR_ERR(rtc->rtc);

	rtc->rtc->ops = &rockchip_rtc_ops;

	rtc->irq = platform_get_irq(pdev, 0);
	if (rtc->irq < 0)
		return dev_err_probe(&pdev->dev, rtc->irq, "No IRQ resource\n");

	/* request alarm irq of rtc */
	ret = devm_request_threaded_irq(&pdev->dev, rtc->irq, NULL,
					&rockchip_rtc_alarm_irq, IRQF_ONESHOT,
					"RTC alarm", rtc);
	if (ret)
		return dev_err_probe(&pdev->dev, ret,
				     "Failed to request alarm IRQ %d\n",
				     rtc->irq);

	return rtc_register_device(rtc->rtc);
}

static struct platform_driver rockchip_rtc_driver = {
	.probe = rockchip_rtc_probe,
	.driver = {
		.name = "rockchip-rtc",
		.pm = &rockchip_rtc_pm_ops,
		.of_match_table = rockchip_rtc_of_match,
	},
};

module_platform_driver(rockchip_rtc_driver);

MODULE_DESCRIPTION("RTC driver for the rockchip");
MODULE_AUTHOR("Zhang Qing <zhangqing@rock-chips.com>");
MODULE_LICENSE("GPL");
