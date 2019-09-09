// SPDX-License-Identifier: GPL-2.0-only
/*
 * An SPI driver for the Philips PCF2123 RTC
 * Copyright 2009 Cyber Switching, Inc.
 *
 * Author: Chris Verges <chrisv@cyberswitching.com>
 * Maintainers: http://www.cyberswitching.com
 *
 * based on the RS5C348 driver in this same directory.
 *
 * Thanks to Christian Pellegrin <chripell@fsfe.org> for
 * the sysfs contributions to this driver.
 *
 * Please note that the CS is active high, so platform data
 * should look something like:
 *
 * static struct spi_board_info ek_spi_devices[] = {
 *	...
 *	{
 *		.modalias		= "rtc-pcf2123",
 *		.chip_select		= 1,
 *		.controller_data	= (void *)AT91_PIN_PA10,
 *		.max_speed_hz		= 1000 * 1000,
 *		.mode			= SPI_CS_HIGH,
 *		.bus_num		= 0,
 *	},
 *	...
 *};
 */

#include <linux/bcd.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/rtc.h>
#include <linux/spi/spi.h>
#include <linux/module.h>
#include <linux/regmap.h>

/* REGISTERS */
#define PCF2123_REG_CTRL1	(0x00)	/* Control Register 1 */
#define PCF2123_REG_CTRL2	(0x01)	/* Control Register 2 */
#define PCF2123_REG_SC		(0x02)	/* datetime */
#define PCF2123_REG_MN		(0x03)
#define PCF2123_REG_HR		(0x04)
#define PCF2123_REG_DM		(0x05)
#define PCF2123_REG_DW		(0x06)
#define PCF2123_REG_MO		(0x07)
#define PCF2123_REG_YR		(0x08)
#define PCF2123_REG_ALRM_MN	(0x09)	/* Alarm Registers */
#define PCF2123_REG_ALRM_HR	(0x0a)
#define PCF2123_REG_ALRM_DM	(0x0b)
#define PCF2123_REG_ALRM_DW	(0x0c)
#define PCF2123_REG_OFFSET	(0x0d)	/* Clock Rate Offset Register */
#define PCF2123_REG_TMR_CLKOUT	(0x0e)	/* Timer Registers */
#define PCF2123_REG_CTDWN_TMR	(0x0f)

/* PCF2123_REG_CTRL1 BITS */
#define CTRL1_CLEAR		(0)	/* Clear */
#define CTRL1_CORR_INT		BIT(1)	/* Correction irq enable */
#define CTRL1_12_HOUR		BIT(2)	/* 12 hour time */
#define CTRL1_SW_RESET	(BIT(3) | BIT(4) | BIT(6))	/* Software reset */
#define CTRL1_STOP		BIT(5)	/* Stop the clock */
#define CTRL1_EXT_TEST		BIT(7)	/* External clock test mode */

/* PCF2123_REG_CTRL2 BITS */
#define CTRL2_TIE		BIT(0)	/* Countdown timer irq enable */
#define CTRL2_AIE		BIT(1)	/* Alarm irq enable */
#define CTRL2_TF		BIT(2)	/* Countdown timer flag */
#define CTRL2_AF		BIT(3)	/* Alarm flag */
#define CTRL2_TI_TP		BIT(4)	/* Irq pin generates pulse */
#define CTRL2_MSF		BIT(5)	/* Minute or second irq flag */
#define CTRL2_SI		BIT(6)	/* Second irq enable */
#define CTRL2_MI		BIT(7)	/* Minute irq enable */

/* PCF2123_REG_SC BITS */
#define OSC_HAS_STOPPED		BIT(7)	/* Clock has been stopped */

/* PCF2123_REG_ALRM_XX BITS */
#define ALRM_ENABLE		BIT(7)	/* MN, HR, DM, or DW alarm enable */

/* PCF2123_REG_TMR_CLKOUT BITS */
#define CD_TMR_4096KHZ		(0)	/* 4096 KHz countdown timer */
#define CD_TMR_64HZ		(1)	/* 64 Hz countdown timer */
#define CD_TMR_1HZ		(2)	/* 1 Hz countdown timer */
#define CD_TMR_60th_HZ		(3)	/* 60th Hz countdown timer */
#define CD_TMR_TE		BIT(3)	/* Countdown timer enable */

/* PCF2123_REG_OFFSET BITS */
#define OFFSET_SIGN_BIT		6	/* 2's complement sign bit */
#define OFFSET_COARSE		BIT(7)	/* Coarse mode offset */
#define OFFSET_STEP		(2170)	/* Offset step in parts per billion */
#define OFFSET_MASK		GENMASK(6, 0)	/* Offset value */

/* READ/WRITE ADDRESS BITS */
#define PCF2123_WRITE		BIT(4)
#define PCF2123_READ		(BIT(4) | BIT(7))


static struct spi_driver pcf2123_driver;

struct pcf2123_plat_data {
	struct rtc_device *rtc;
	struct regmap *map;
};

static const struct regmap_config pcf2123_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.read_flag_mask = PCF2123_READ,
	.write_flag_mask = PCF2123_WRITE,
	.max_register = PCF2123_REG_CTDWN_TMR,
};

static int pcf2123_read_offset(struct device *dev, long *offset)
{
	struct pcf2123_plat_data *pdata = dev_get_platdata(dev);
	int ret, val;
	unsigned int reg;

	ret = regmap_read(pdata->map, PCF2123_REG_OFFSET, &reg);
	if (ret)
		return ret;

	val = sign_extend32((reg & OFFSET_MASK), OFFSET_SIGN_BIT);

	if (reg & OFFSET_COARSE)
		val *= 2;

	*offset = ((long)val) * OFFSET_STEP;

	return 0;
}

/*
 * The offset register is a 7 bit signed value with a coarse bit in bit 7.
 * The main difference between the two is normal offset adjusts the first
 * second of n minutes every other hour, with 61, 62 and 63 being shoved
 * into the 60th minute.
 * The coarse adjustment does the same, but every hour.
 * the two overlap, with every even normal offset value corresponding
 * to a coarse offset. Based on this algorithm, it seems that despite the
 * name, coarse offset is a better fit for overlapping values.
 */
static int pcf2123_set_offset(struct device *dev, long offset)
{
	struct pcf2123_plat_data *pdata = dev_get_platdata(dev);
	s8 reg;

	if (offset > OFFSET_STEP * 127)
		reg = 127;
	else if (offset < OFFSET_STEP * -128)
		reg = -128;
	else
		reg = DIV_ROUND_CLOSEST(offset, OFFSET_STEP);

	/* choose fine offset only for odd values in the normal range */
	if (reg & 1 && reg <= 63 && reg >= -64) {
		/* Normal offset. Clear the coarse bit */
		reg &= ~OFFSET_COARSE;
	} else {
		/* Coarse offset. Divide by 2 and set the coarse bit */
		reg >>= 1;
		reg |= OFFSET_COARSE;
	}

	return regmap_write(pdata->map, PCF2123_REG_OFFSET, (unsigned int)reg);
}

static int pcf2123_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
	struct pcf2123_plat_data *pdata = dev_get_platdata(dev);
	u8 rxbuf[7];
	int ret;

	ret = regmap_bulk_read(pdata->map, PCF2123_REG_SC, rxbuf,
				sizeof(rxbuf));
	if (ret)
		return ret;

	if (rxbuf[0] & OSC_HAS_STOPPED) {
		dev_info(dev, "clock was stopped. Time is not valid\n");
		return -EINVAL;
	}

	tm->tm_sec = bcd2bin(rxbuf[0] & 0x7F);
	tm->tm_min = bcd2bin(rxbuf[1] & 0x7F);
	tm->tm_hour = bcd2bin(rxbuf[2] & 0x3F); /* rtc hr 0-23 */
	tm->tm_mday = bcd2bin(rxbuf[3] & 0x3F);
	tm->tm_wday = rxbuf[4] & 0x07;
	tm->tm_mon = bcd2bin(rxbuf[5] & 0x1F) - 1; /* rtc mn 1-12 */
	tm->tm_year = bcd2bin(rxbuf[6]);
	if (tm->tm_year < 70)
		tm->tm_year += 100;	/* assume we are in 1970...2069 */

	dev_dbg(dev, "%s: tm is %ptR\n", __func__, tm);

	return 0;
}

static int pcf2123_rtc_set_time(struct device *dev, struct rtc_time *tm)
{
	struct pcf2123_plat_data *pdata = dev_get_platdata(dev);
	u8 txbuf[7];
	int ret;

	dev_dbg(dev, "%s: tm is %ptR\n", __func__, tm);

	/* Stop the counter first */
	ret = regmap_write(pdata->map, PCF2123_REG_CTRL1, CTRL1_STOP);
	if (ret)
		return ret;

	/* Set the new time */
	txbuf[0] = bin2bcd(tm->tm_sec & 0x7F);
	txbuf[1] = bin2bcd(tm->tm_min & 0x7F);
	txbuf[2] = bin2bcd(tm->tm_hour & 0x3F);
	txbuf[3] = bin2bcd(tm->tm_mday & 0x3F);
	txbuf[4] = tm->tm_wday & 0x07;
	txbuf[5] = bin2bcd((tm->tm_mon + 1) & 0x1F); /* rtc mn 1-12 */
	txbuf[6] = bin2bcd(tm->tm_year < 100 ? tm->tm_year : tm->tm_year - 100);

	ret = regmap_bulk_write(pdata->map, PCF2123_REG_SC, txbuf,
				sizeof(txbuf));
	if (ret)
		return ret;

	/* Start the counter */
	ret = regmap_write(pdata->map, PCF2123_REG_CTRL1, CTRL1_CLEAR);
	if (ret)
		return ret;

	return 0;
}

static int pcf2123_rtc_read_alarm(struct device *dev, struct rtc_wkalrm *alm)
{
	struct pcf2123_plat_data *pdata = dev_get_platdata(dev);
	u8 rxbuf[4];
	int ret;
	unsigned int val = 0;

	ret = regmap_bulk_read(pdata->map, PCF2123_REG_ALRM_MN, rxbuf,
				sizeof(rxbuf));
	if (ret)
		return ret;

	alm->time.tm_min = bcd2bin(rxbuf[0] & 0x7F);
	alm->time.tm_hour = bcd2bin(rxbuf[1] & 0x3F);
	alm->time.tm_mday = bcd2bin(rxbuf[2] & 0x3F);
	alm->time.tm_wday = bcd2bin(rxbuf[3] & 0x07);

	dev_dbg(dev, "%s: alm is %ptR\n", __func__, &alm->time);

	ret = regmap_read(pdata->map, PCF2123_REG_CTRL2, &val);
	if (ret)
		return ret;

	alm->enabled = !!(val & CTRL2_AIE);

	return 0;
}

static int pcf2123_rtc_set_alarm(struct device *dev, struct rtc_wkalrm *alm)
{
	struct pcf2123_plat_data *pdata = dev_get_platdata(dev);
	u8 txbuf[4];
	int ret;

	dev_dbg(dev, "%s: alm is %ptR\n", __func__, &alm->time);

	/* Ensure alarm flag is clear */
	ret = regmap_update_bits(pdata->map, PCF2123_REG_CTRL2, CTRL2_AF, 0);
	if (ret)
		return ret;

	/* Disable alarm interrupt */
	ret = regmap_update_bits(pdata->map, PCF2123_REG_CTRL2, CTRL2_AIE, 0);
	if (ret)
		return ret;

	/* Set new alarm */
	txbuf[0] = bin2bcd(alm->time.tm_min & 0x7F);
	txbuf[1] = bin2bcd(alm->time.tm_hour & 0x3F);
	txbuf[2] = bin2bcd(alm->time.tm_mday & 0x3F);
	txbuf[3] = bin2bcd(alm->time.tm_wday & 0x07);

	ret = regmap_bulk_write(pdata->map, PCF2123_REG_ALRM_MN, txbuf,
				sizeof(txbuf));
	if (ret)
		return ret;

	/* Enable alarm interrupt */
	if (alm->enabled)	{
		ret = regmap_update_bits(pdata->map, PCF2123_REG_CTRL2,
						CTRL2_AIE, CTRL2_AIE);
		if (ret)
			return ret;
	}

	return 0;
}

static irqreturn_t pcf2123_rtc_irq(int irq, void *dev)
{
	struct pcf2123_plat_data *pdata = dev_get_platdata(dev);
	struct mutex *lock = &pdata->rtc->ops_lock;
	unsigned int val = 0;
	int ret = IRQ_NONE;

	mutex_lock(lock);
	regmap_read(pdata->map, PCF2123_REG_CTRL2, &val);

	/* Alarm? */
	if (val & CTRL2_AF) {
		ret = IRQ_HANDLED;

		/* Clear alarm flag */
		regmap_update_bits(pdata->map, PCF2123_REG_CTRL2, CTRL2_AF, 0);

		rtc_update_irq(pdata->rtc, 1, RTC_IRQF | RTC_AF);
	}

	mutex_unlock(lock);

	return ret;
}

static int pcf2123_reset(struct device *dev)
{
	struct pcf2123_plat_data *pdata = dev_get_platdata(dev);
	int ret;
	unsigned int val = 0;

	ret = regmap_write(pdata->map, PCF2123_REG_CTRL1, CTRL1_SW_RESET);
	if (ret)
		return ret;

	/* Stop the counter */
	dev_dbg(dev, "stopping RTC\n");
	ret = regmap_write(pdata->map, PCF2123_REG_CTRL1, CTRL1_STOP);
	if (ret)
		return ret;

	/* See if the counter was actually stopped */
	dev_dbg(dev, "checking for presence of RTC\n");
	ret = regmap_read(pdata->map, PCF2123_REG_CTRL1, &val);
	if (ret)
		return ret;

	dev_dbg(dev, "received data from RTC (0x%08X)\n", val);
	if (!(val & CTRL1_STOP))
		return -ENODEV;

	/* Start the counter */
	ret = regmap_write(pdata->map, PCF2123_REG_CTRL1, CTRL1_CLEAR);
	if (ret)
		return ret;

	return 0;
}

static const struct rtc_class_ops pcf2123_rtc_ops = {
	.read_time	= pcf2123_rtc_read_time,
	.set_time	= pcf2123_rtc_set_time,
	.read_offset	= pcf2123_read_offset,
	.set_offset	= pcf2123_set_offset,
	.read_alarm	= pcf2123_rtc_read_alarm,
	.set_alarm	= pcf2123_rtc_set_alarm,
};

static int pcf2123_probe(struct spi_device *spi)
{
	struct rtc_device *rtc;
	struct rtc_time tm;
	struct pcf2123_plat_data *pdata;
	int ret = 0;

	pdata = devm_kzalloc(&spi->dev, sizeof(struct pcf2123_plat_data),
				GFP_KERNEL);
	if (!pdata)
		return -ENOMEM;
	spi->dev.platform_data = pdata;

	pdata->map = devm_regmap_init_spi(spi, &pcf2123_regmap_config);

	if (IS_ERR(pdata->map)) {
		dev_err(&spi->dev, "regmap init failed.\n");
		goto kfree_exit;
	}

	ret = pcf2123_rtc_read_time(&spi->dev, &tm);
	if (ret < 0) {
		ret = pcf2123_reset(&spi->dev);
		if (ret < 0) {
			dev_err(&spi->dev, "chip not found\n");
			goto kfree_exit;
		}
	}

	dev_info(&spi->dev, "spiclk %u KHz.\n",
			(spi->max_speed_hz + 500) / 1000);

	/* Finalize the initialization */
	rtc = devm_rtc_device_register(&spi->dev, pcf2123_driver.driver.name,
			&pcf2123_rtc_ops, THIS_MODULE);

	if (IS_ERR(rtc)) {
		dev_err(&spi->dev, "failed to register.\n");
		ret = PTR_ERR(rtc);
		goto kfree_exit;
	}

	pdata->rtc = rtc;

	/* Register alarm irq */
	if (spi->irq > 0) {
		ret = devm_request_threaded_irq(&spi->dev, spi->irq, NULL,
				pcf2123_rtc_irq,
				IRQF_TRIGGER_LOW | IRQF_ONESHOT,
				pcf2123_driver.driver.name, &spi->dev);
		if (!ret)
			device_init_wakeup(&spi->dev, true);
		else
			dev_err(&spi->dev, "could not request irq.\n");
	}

	/* The PCF2123's alarm only has minute accuracy. Must add timer
	 * support to this driver to generate interrupts more than once
	 * per minute.
	 */
	pdata->rtc->uie_unsupported = 1;

	return 0;

kfree_exit:
	spi->dev.platform_data = NULL;
	return ret;
}

#ifdef CONFIG_OF
static const struct of_device_id pcf2123_dt_ids[] = {
	{ .compatible = "nxp,rtc-pcf2123", },
	{ .compatible = "microcrystal,rv2123", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, pcf2123_dt_ids);
#endif

static struct spi_driver pcf2123_driver = {
	.driver	= {
			.name	= "rtc-pcf2123",
			.of_match_table = of_match_ptr(pcf2123_dt_ids),
	},
	.probe	= pcf2123_probe,
};

module_spi_driver(pcf2123_driver);

MODULE_AUTHOR("Chris Verges <chrisv@cyberswitching.com>");
MODULE_DESCRIPTION("NXP PCF2123 RTC driver");
MODULE_LICENSE("GPL");
