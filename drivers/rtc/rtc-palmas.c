/*
 * rtc-palmas.c -- Palmas Real Time Clock driver.

 * RTC driver for TI Palma series devices like TPS65913,
 * TPS65914 power management IC.
 *
 * Copyright (c) 2012, NVIDIA Corporation.
 *
 * Author: Laxman Dewangan <ldewangan@nvidia.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any kind,
 * whether express or implied; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
 * 02111-1307, USA
 */

#include <linux/bcd.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/mfd/palmas.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/rtc.h>
#include <linux/types.h>
#include <linux/platform_device.h>
#include <linux/pm.h>

struct palmas_rtc {
	struct rtc_device	*rtc;
	struct device		*dev;
	unsigned int		irq;
};

/* Total number of RTC registers needed to set time*/
#define PALMAS_NUM_TIME_REGS	(PALMAS_YEARS_REG - PALMAS_SECONDS_REG + 1)

/*
 * Special bin2bcd mapping to deal with bcd storage of year.
 *
 *   0-69                -> 0xD0
 *  70-99  (1970 - 1999) -> 0xD0 - 0xF9 (correctly rolls to 0x00)
 * 100-199 (2000 - 2099) -> 0x00 - 0x99 (does not roll to 0xA0 :-( )
 * 200-229 (2100 - 2129) -> 0xA0 - 0xC9 (really for completeness)
 * 230-                  -> 0xC9
 *
 * Confirmed: the only transition that does not work correctly for this rtc
 * clock is the transition from 2099 to 2100, it proceeds to 2000. We will
 * accept this issue since the clock retains and transitions the year correctly
 * in all other conditions.
 */
static unsigned char year_bin2bcd(int val)
{
	if (val < 70)
		return 0xD0;
	if (val < 100)
		return bin2bcd(val - 20) | 0x80; /* KISS leverage of bin2bcd */
	if (val >= 230)
		return 0xC9;
	if (val >= 200)
		return bin2bcd(val - 180) | 0x80;
	return bin2bcd(val - 100);
}

static int year_bcd2bin(unsigned char val)
{
	if (val >= 0xD0)
		return bcd2bin(val & 0x7F) + 20;
	if (val >= 0xA0)
		return bcd2bin(val & 0x7F) + 180;
	return bcd2bin(val) + 100;
}

static int palmas_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
	unsigned char rtc_data[PALMAS_NUM_TIME_REGS];
	struct palmas *palmas = dev_get_drvdata(dev->parent);
	int ret;

	/* Copy RTC counting registers to static registers or latches */
	ret = palmas_update_bits(palmas, PALMAS_RTC_BASE, PALMAS_RTC_CTRL_REG,
		PALMAS_RTC_CTRL_REG_GET_TIME, PALMAS_RTC_CTRL_REG_GET_TIME);
	if (ret < 0) {
		dev_err(dev, "RTC CTRL reg update failed, err: %d\n", ret);
		return ret;
	}

	ret = palmas_bulk_read(palmas, PALMAS_RTC_BASE, PALMAS_SECONDS_REG,
			rtc_data, PALMAS_NUM_TIME_REGS);
	if (ret < 0) {
		dev_err(dev, "RTC_SECONDS reg read failed, err = %d\n", ret);
		return ret;
	}

	tm->tm_sec = bcd2bin(rtc_data[0]);
	tm->tm_min = bcd2bin(rtc_data[1]);
	tm->tm_hour = bcd2bin(rtc_data[2]);
	tm->tm_mday = bcd2bin(rtc_data[3]);
	tm->tm_mon = bcd2bin(rtc_data[4]) - 1;
	tm->tm_year = year_bcd2bin(rtc_data[5]);

	return ret;
}

static int palmas_rtc_set_time(struct device *dev, struct rtc_time *tm)
{
	unsigned char rtc_data[PALMAS_NUM_TIME_REGS];
	struct palmas *palmas = dev_get_drvdata(dev->parent);
	int ret;

	rtc_data[0] = bin2bcd(tm->tm_sec);
	rtc_data[1] = bin2bcd(tm->tm_min);
	rtc_data[2] = bin2bcd(tm->tm_hour);
	rtc_data[3] = bin2bcd(tm->tm_mday);
	rtc_data[4] = bin2bcd(tm->tm_mon + 1);
	rtc_data[5] = year_bin2bcd(tm->tm_year);

	/* Stop RTC while updating the RTC time registers */
	ret = palmas_update_bits(palmas, PALMAS_RTC_BASE, PALMAS_RTC_CTRL_REG,
		PALMAS_RTC_CTRL_REG_STOP_RTC, 0);
	if (ret < 0) {
		dev_err(dev, "RTC stop failed, err = %d\n", ret);
		return ret;
	}

	ret = palmas_bulk_write(palmas, PALMAS_RTC_BASE, PALMAS_SECONDS_REG,
		rtc_data, PALMAS_NUM_TIME_REGS);
	if (ret < 0) {
		dev_err(dev, "RTC_SECONDS reg write failed, err = %d\n", ret);
		return ret;
	}

	/* Start back RTC */
	ret = palmas_update_bits(palmas, PALMAS_RTC_BASE, PALMAS_RTC_CTRL_REG,
		PALMAS_RTC_CTRL_REG_STOP_RTC, PALMAS_RTC_CTRL_REG_STOP_RTC);
	if (ret < 0)
		dev_err(dev, "RTC start failed, err = %d\n", ret);
	return ret;
}

static int palmas_rtc_alarm_irq_enable(struct device *dev, unsigned enabled)
{
	struct palmas *palmas = dev_get_drvdata(dev->parent);
	u8 val;

	val = enabled ? PALMAS_RTC_INTERRUPTS_REG_IT_ALARM : 0;
	return palmas_write(palmas, PALMAS_RTC_BASE,
		PALMAS_RTC_INTERRUPTS_REG, val);
}

static int palmas_rtc_read_alarm(struct device *dev, struct rtc_wkalrm *alm)
{
	unsigned char alarm_data[PALMAS_NUM_TIME_REGS];
	u32 int_val;
	struct palmas *palmas = dev_get_drvdata(dev->parent);
	int ret;

	ret = palmas_bulk_read(palmas, PALMAS_RTC_BASE,
			PALMAS_ALARM_SECONDS_REG,
			alarm_data, PALMAS_NUM_TIME_REGS);
	if (ret < 0) {
		dev_err(dev, "RTC_ALARM_SECONDS read failed, err = %d\n", ret);
		return ret;
	}

	alm->time.tm_sec = bcd2bin(alarm_data[0]);
	alm->time.tm_min = bcd2bin(alarm_data[1]);
	alm->time.tm_hour = bcd2bin(alarm_data[2]);
	alm->time.tm_mday = bcd2bin(alarm_data[3]);
	alm->time.tm_mon = bcd2bin(alarm_data[4]) - 1;
	alm->time.tm_year = year_bcd2bin(alarm_data[5]);

	ret = palmas_read(palmas, PALMAS_RTC_BASE, PALMAS_RTC_INTERRUPTS_REG,
			&int_val);
	if (ret < 0) {
		dev_err(dev, "RTC_INTERRUPTS reg read failed, err = %d\n", ret);
		return ret;
	}

	if (int_val & PALMAS_RTC_INTERRUPTS_REG_IT_ALARM)
		alm->enabled = 1;
	return ret;
}

static int palmas_rtc_set_alarm(struct device *dev, struct rtc_wkalrm *alm)
{
	unsigned char alarm_data[PALMAS_NUM_TIME_REGS];
	struct palmas *palmas = dev_get_drvdata(dev->parent);
	int ret;

	ret = palmas_rtc_alarm_irq_enable(dev, 0);
	if (ret < 0) {
		dev_err(dev, "Disable RTC alarm failed\n");
		return ret;
	}

	alarm_data[0] = bin2bcd(alm->time.tm_sec);
	alarm_data[1] = bin2bcd(alm->time.tm_min);
	alarm_data[2] = bin2bcd(alm->time.tm_hour);
	alarm_data[3] = bin2bcd(alm->time.tm_mday);
	alarm_data[4] = bin2bcd(alm->time.tm_mon + 1);
	alarm_data[5] = year_bin2bcd(alm->time.tm_year);

	ret = palmas_bulk_write(palmas, PALMAS_RTC_BASE,
		PALMAS_ALARM_SECONDS_REG, alarm_data, PALMAS_NUM_TIME_REGS);
	if (ret < 0) {
		dev_err(dev, "ALARM_SECONDS_REG write failed, err = %d\n", ret);
		return ret;
	}

	if (alm->enabled)
		ret = palmas_rtc_alarm_irq_enable(dev, 1);
	return ret;
}

static int palmas_clear_interrupts(struct device *dev)
{
	struct palmas *palmas = dev_get_drvdata(dev->parent);
	unsigned int rtc_reg;
	int ret;

	ret = palmas_read(palmas, PALMAS_RTC_BASE, PALMAS_RTC_STATUS_REG,
				&rtc_reg);
	if (ret < 0) {
		dev_err(dev, "RTC_STATUS read failed, err = %d\n", ret);
		return ret;
	}

	ret = palmas_write(palmas, PALMAS_RTC_BASE, PALMAS_RTC_STATUS_REG,
			rtc_reg);
	if (ret < 0) {
		dev_err(dev, "RTC_STATUS write failed, err = %d\n", ret);
		return ret;
	}
	return 0;
}

static irqreturn_t palmas_rtc_interrupt(int irq, void *context)
{
	struct palmas_rtc *palmas_rtc = context;
	struct device *dev = palmas_rtc->dev;
	int ret;

	ret = palmas_clear_interrupts(dev);
	if (ret < 0) {
		dev_err(dev, "RTC interrupt clear failed, err = %d\n", ret);
		return IRQ_NONE;
	}

	rtc_update_irq(palmas_rtc->rtc, 1, RTC_IRQF | RTC_AF);
	return IRQ_HANDLED;
}

static const struct rtc_class_ops palmas_rtc_ops = {
	.read_time	= palmas_rtc_read_time,
	.set_time	= palmas_rtc_set_time,
	.read_alarm	= palmas_rtc_read_alarm,
	.set_alarm	= palmas_rtc_set_alarm,
	.alarm_irq_enable = palmas_rtc_alarm_irq_enable,
};

static int palmas_rtc_probe(struct platform_device *pdev)
{
	struct palmas *palmas = dev_get_drvdata(pdev->dev.parent);
	struct palmas_rtc *palmas_rtc = NULL;
	int ret;
	bool enable_bb_charging = false;
	bool high_bb_charging = false;

	if (pdev->dev.of_node) {
		enable_bb_charging = of_property_read_bool(pdev->dev.of_node,
					"ti,backup-battery-chargeable");
		high_bb_charging = of_property_read_bool(pdev->dev.of_node,
					"ti,backup-battery-charge-high-current");
	}

	palmas_rtc = devm_kzalloc(&pdev->dev, sizeof(struct palmas_rtc),
			GFP_KERNEL);
	if (!palmas_rtc)
		return -ENOMEM;

	/* Clear pending interrupts */
	ret = palmas_clear_interrupts(&pdev->dev);
	if (ret < 0) {
		dev_err(&pdev->dev, "clear RTC int failed, err = %d\n", ret);
		return ret;
	}

	palmas_rtc->dev = &pdev->dev;
	platform_set_drvdata(pdev, palmas_rtc);

	if (enable_bb_charging) {
		unsigned reg = PALMAS_BACKUP_BATTERY_CTRL_BBS_BBC_LOW_ICHRG;

		if (high_bb_charging)
			reg = 0;

		ret = palmas_update_bits(palmas, PALMAS_PMU_CONTROL_BASE,
			PALMAS_BACKUP_BATTERY_CTRL,
			PALMAS_BACKUP_BATTERY_CTRL_BBS_BBC_LOW_ICHRG, reg);
		if (ret < 0) {
			dev_err(&pdev->dev,
				"BACKUP_BATTERY_CTRL update failed, %d\n", ret);
			return ret;
		}

		ret = palmas_update_bits(palmas, PALMAS_PMU_CONTROL_BASE,
			PALMAS_BACKUP_BATTERY_CTRL,
			PALMAS_BACKUP_BATTERY_CTRL_BB_CHG_EN,
			PALMAS_BACKUP_BATTERY_CTRL_BB_CHG_EN);
		if (ret < 0) {
			dev_err(&pdev->dev,
				"BACKUP_BATTERY_CTRL update failed, %d\n", ret);
			return ret;
		}
	}

	/* Start RTC */
	ret = palmas_update_bits(palmas, PALMAS_RTC_BASE, PALMAS_RTC_CTRL_REG,
			PALMAS_RTC_CTRL_REG_STOP_RTC,
			PALMAS_RTC_CTRL_REG_STOP_RTC);
	if (ret < 0) {
		dev_err(&pdev->dev, "RTC_CTRL write failed, err = %d\n", ret);
		return ret;
	}

	palmas_rtc->irq = platform_get_irq(pdev, 0);

	device_init_wakeup(&pdev->dev, 1);
	palmas_rtc->rtc = devm_rtc_device_register(&pdev->dev, pdev->name,
				&palmas_rtc_ops, THIS_MODULE);
	if (IS_ERR(palmas_rtc->rtc)) {
		ret = PTR_ERR(palmas_rtc->rtc);
		dev_err(&pdev->dev, "RTC register failed, err = %d\n", ret);
		return ret;
	}

	ret = devm_request_threaded_irq(&pdev->dev, palmas_rtc->irq, NULL,
			palmas_rtc_interrupt,
			IRQF_TRIGGER_LOW | IRQF_ONESHOT,
			dev_name(&pdev->dev), palmas_rtc);
	if (ret < 0) {
		dev_err(&pdev->dev, "IRQ request failed, err = %d\n", ret);
		return ret;
	}

	return 0;
}

static int palmas_rtc_remove(struct platform_device *pdev)
{
	palmas_rtc_alarm_irq_enable(&pdev->dev, 0);
	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int palmas_rtc_suspend(struct device *dev)
{
	struct palmas_rtc *palmas_rtc = dev_get_drvdata(dev);

	if (device_may_wakeup(dev))
		enable_irq_wake(palmas_rtc->irq);
	return 0;
}

static int palmas_rtc_resume(struct device *dev)
{
	struct palmas_rtc *palmas_rtc = dev_get_drvdata(dev);

	if (device_may_wakeup(dev))
		disable_irq_wake(palmas_rtc->irq);
	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(palmas_rtc_pm_ops, palmas_rtc_suspend,
			 palmas_rtc_resume);

#ifdef CONFIG_OF
static const struct of_device_id of_palmas_rtc_match[] = {
	{ .compatible = "ti,palmas-rtc"},
	{ },
};
MODULE_DEVICE_TABLE(of, of_palmas_rtc_match);
#endif

static struct platform_driver palmas_rtc_driver = {
	.probe		= palmas_rtc_probe,
	.remove		= palmas_rtc_remove,
	.driver		= {
		.name	= "palmas-rtc",
		.pm	= &palmas_rtc_pm_ops,
		.of_match_table = of_match_ptr(of_palmas_rtc_match),
	},
};

module_platform_driver(palmas_rtc_driver);

MODULE_ALIAS("platform:palmas_rtc");
MODULE_DESCRIPTION("TI PALMAS series RTC driver");
MODULE_AUTHOR("Laxman Dewangan <ldewangan@nvidia.com>");
MODULE_LICENSE("GPL v2");
