// SPDX-License-Identifier: GPL-2.0-only
/*
 * SPI Driver for Microchip MCP795 RTC
 *
 * Copyright (C) Josef Gajdusek <atx@atx.name>
 *
 * based on other Linux RTC drivers
 *
 * Device datasheet:
 * https://ww1.microchip.com/downloads/en/DeviceDoc/22280A.pdf
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/printk.h>
#include <linux/spi/spi.h>
#include <linux/rtc.h>
#include <linux/of.h>
#include <linux/bcd.h>
#include <linux/delay.h>

/* MCP795 Instructions, see datasheet table 3-1 */
#define MCP795_EEREAD	0x03
#define MCP795_EEWRITE	0x02
#define MCP795_EEWRDI	0x04
#define MCP795_EEWREN	0x06
#define MCP795_SRREAD	0x05
#define MCP795_SRWRITE	0x01
#define MCP795_READ	0x13
#define MCP795_WRITE	0x12
#define MCP795_UNLOCK	0x14
#define MCP795_IDWRITE	0x32
#define MCP795_IDREAD	0x33
#define MCP795_CLRWDT	0x44
#define MCP795_CLRRAM	0x54

/* MCP795 RTCC registers, see datasheet table 4-1 */
#define MCP795_REG_SECONDS	0x01
#define MCP795_REG_DAY		0x04
#define MCP795_REG_MONTH	0x06
#define MCP795_REG_CONTROL	0x08
#define MCP795_REG_ALM0_SECONDS	0x0C
#define MCP795_REG_ALM0_DAY	0x0F

#define MCP795_ST_BIT		BIT(7)
#define MCP795_24_BIT		BIT(6)
#define MCP795_LP_BIT		BIT(5)
#define MCP795_EXTOSC_BIT	BIT(3)
#define MCP795_OSCON_BIT	BIT(5)
#define MCP795_ALM0_BIT		BIT(4)
#define MCP795_ALM1_BIT		BIT(5)
#define MCP795_ALM0IF_BIT	BIT(3)
#define MCP795_ALM0C0_BIT	BIT(4)
#define MCP795_ALM0C1_BIT	BIT(5)
#define MCP795_ALM0C2_BIT	BIT(6)

#define SEC_PER_DAY		(24 * 60 * 60)

static int mcp795_rtcc_read(struct device *dev, u8 addr, u8 *buf, u8 count)
{
	struct spi_device *spi = to_spi_device(dev);
	int ret;
	u8 tx[2];

	tx[0] = MCP795_READ;
	tx[1] = addr;
	ret = spi_write_then_read(spi, tx, sizeof(tx), buf, count);

	if (ret)
		dev_err(dev, "Failed reading %d bytes from address %x.\n",
					count, addr);

	return ret;
}

static int mcp795_rtcc_write(struct device *dev, u8 addr, u8 *data, u8 count)
{
	struct spi_device *spi = to_spi_device(dev);
	int ret;
	u8 tx[257];

	tx[0] = MCP795_WRITE;
	tx[1] = addr;
	memcpy(&tx[2], data, count);

	ret = spi_write(spi, tx, 2 + count);

	if (ret)
		dev_err(dev, "Failed to write %d bytes to address %x.\n",
					count, addr);

	return ret;
}

static int mcp795_rtcc_set_bits(struct device *dev, u8 addr, u8 mask, u8 state)
{
	int ret;
	u8 tmp;

	ret = mcp795_rtcc_read(dev, addr, &tmp, 1);
	if (ret)
		return ret;

	if ((tmp & mask) != state) {
		tmp = (tmp & ~mask) | state;
		ret = mcp795_rtcc_write(dev, addr, &tmp, 1);
	}

	return ret;
}

static int mcp795_stop_oscillator(struct device *dev, bool *extosc)
{
	int retries = 5;
	int ret;
	u8 data;

	ret = mcp795_rtcc_set_bits(dev, MCP795_REG_SECONDS, MCP795_ST_BIT, 0);
	if (ret)
		return ret;
	ret = mcp795_rtcc_read(dev, MCP795_REG_CONTROL, &data, 1);
	if (ret)
		return ret;
	*extosc = !!(data & MCP795_EXTOSC_BIT);
	ret = mcp795_rtcc_set_bits(
				dev, MCP795_REG_CONTROL, MCP795_EXTOSC_BIT, 0);
	if (ret)
		return ret;
	/* wait for the OSCON bit to clear */
	do {
		usleep_range(700, 800);
		ret = mcp795_rtcc_read(dev, MCP795_REG_DAY, &data, 1);
		if (ret)
			break;
		if (!(data & MCP795_OSCON_BIT))
			break;

	} while (--retries);

	return !retries ? -EIO : ret;
}

static int mcp795_start_oscillator(struct device *dev, bool *extosc)
{
	if (extosc) {
		u8 data = *extosc ? MCP795_EXTOSC_BIT : 0;
		int ret;

		ret = mcp795_rtcc_set_bits(
			dev, MCP795_REG_CONTROL, MCP795_EXTOSC_BIT, data);
		if (ret)
			return ret;
	}
	return mcp795_rtcc_set_bits(
			dev, MCP795_REG_SECONDS, MCP795_ST_BIT, MCP795_ST_BIT);
}

/* Enable or disable Alarm 0 in RTC */
static int mcp795_update_alarm(struct device *dev, bool enable)
{
	int ret;

	dev_dbg(dev, "%s alarm\n", enable ? "Enable" : "Disable");

	if (enable) {
		/* clear ALM0IF (Alarm 0 Interrupt Flag) bit */
		ret = mcp795_rtcc_set_bits(dev, MCP795_REG_ALM0_DAY,
					MCP795_ALM0IF_BIT, 0);
		if (ret)
			return ret;
		/* enable alarm 0 */
		ret = mcp795_rtcc_set_bits(dev, MCP795_REG_CONTROL,
					MCP795_ALM0_BIT, MCP795_ALM0_BIT);
	} else {
		/* disable alarm 0 and alarm 1 */
		ret = mcp795_rtcc_set_bits(dev, MCP795_REG_CONTROL,
					MCP795_ALM0_BIT | MCP795_ALM1_BIT, 0);
	}
	return ret;
}

static int mcp795_set_time(struct device *dev, struct rtc_time *tim)
{
	int ret;
	u8 data[7];
	bool extosc;

	/* Stop RTC and store current value of EXTOSC bit */
	ret = mcp795_stop_oscillator(dev, &extosc);
	if (ret)
		return ret;

	/* Read first, so we can leave config bits untouched */
	ret = mcp795_rtcc_read(dev, MCP795_REG_SECONDS, data, sizeof(data));

	if (ret)
		return ret;

	data[0] = (data[0] & 0x80) | bin2bcd(tim->tm_sec);
	data[1] = (data[1] & 0x80) | bin2bcd(tim->tm_min);
	data[2] = bin2bcd(tim->tm_hour);
	data[3] = (data[3] & 0xF8) | bin2bcd(tim->tm_wday + 1);
	data[4] = bin2bcd(tim->tm_mday);
	data[5] = (data[5] & MCP795_LP_BIT) | bin2bcd(tim->tm_mon + 1);

	if (tim->tm_year > 100)
		tim->tm_year -= 100;

	data[6] = bin2bcd(tim->tm_year);

	/* Always write the date and month using a separate Write command.
	 * This is a workaround for a know silicon issue that some combinations
	 * of date and month values may result in the date being reset to 1.
	 */
	ret = mcp795_rtcc_write(dev, MCP795_REG_SECONDS, data, 5);
	if (ret)
		return ret;

	ret = mcp795_rtcc_write(dev, MCP795_REG_MONTH, &data[5], 2);
	if (ret)
		return ret;

	/* Start back RTC and restore previous value of EXTOSC bit.
	 * There is no need to clear EXTOSC bit when the previous value was 0
	 * because it was already cleared when stopping the RTC oscillator.
	 */
	ret = mcp795_start_oscillator(dev, extosc ? &extosc : NULL);
	if (ret)
		return ret;

	dev_dbg(dev, "Set mcp795: %ptR\n", tim);

	return 0;
}

static int mcp795_read_time(struct device *dev, struct rtc_time *tim)
{
	int ret;
	u8 data[7];

	ret = mcp795_rtcc_read(dev, MCP795_REG_SECONDS, data, sizeof(data));

	if (ret)
		return ret;

	tim->tm_sec	= bcd2bin(data[0] & 0x7F);
	tim->tm_min	= bcd2bin(data[1] & 0x7F);
	tim->tm_hour	= bcd2bin(data[2] & 0x3F);
	tim->tm_wday	= bcd2bin(data[3] & 0x07) - 1;
	tim->tm_mday	= bcd2bin(data[4] & 0x3F);
	tim->tm_mon	= bcd2bin(data[5] & 0x1F) - 1;
	tim->tm_year	= bcd2bin(data[6]) + 100; /* Assume we are in 20xx */

	dev_dbg(dev, "Read from mcp795: %ptR\n", tim);

	return 0;
}

static int mcp795_set_alarm(struct device *dev, struct rtc_wkalrm *alm)
{
	struct rtc_time now_tm;
	time64_t now;
	time64_t later;
	u8 tmp[6];
	int ret;

	/* Read current time from RTC hardware */
	ret = mcp795_read_time(dev, &now_tm);
	if (ret)
		return ret;
	/* Get the number of seconds since 1970 */
	now = rtc_tm_to_time64(&now_tm);
	later = rtc_tm_to_time64(&alm->time);
	if (later <= now)
		return -EINVAL;
	/* make sure alarm fires within the next one year */
	if ((later - now) >=
		(SEC_PER_DAY * (365 + is_leap_year(alm->time.tm_year))))
		return -EDOM;
	/* disable alarm */
	ret = mcp795_update_alarm(dev, false);
	if (ret)
		return ret;
	/* Read registers, so we can leave configuration bits untouched */
	ret = mcp795_rtcc_read(dev, MCP795_REG_ALM0_SECONDS, tmp, sizeof(tmp));
	if (ret)
		return ret;

	alm->time.tm_year	= -1;
	alm->time.tm_isdst	= -1;
	alm->time.tm_yday	= -1;

	tmp[0] = (tmp[0] & 0x80) | bin2bcd(alm->time.tm_sec);
	tmp[1] = (tmp[1] & 0x80) | bin2bcd(alm->time.tm_min);
	tmp[2] = (tmp[2] & 0xE0) | bin2bcd(alm->time.tm_hour);
	tmp[3] = (tmp[3] & 0x80) | bin2bcd(alm->time.tm_wday + 1);
	/* set alarm match: seconds, minutes, hour, day, date and month */
	tmp[3] |= (MCP795_ALM0C2_BIT | MCP795_ALM0C1_BIT | MCP795_ALM0C0_BIT);
	tmp[4] = (tmp[4] & 0xC0) | bin2bcd(alm->time.tm_mday);
	tmp[5] = (tmp[5] & 0xE0) | bin2bcd(alm->time.tm_mon + 1);

	ret = mcp795_rtcc_write(dev, MCP795_REG_ALM0_SECONDS, tmp, sizeof(tmp));
	if (ret)
		return ret;

	/* enable alarm if requested */
	if (alm->enabled) {
		ret = mcp795_update_alarm(dev, true);
		if (ret)
			return ret;
		dev_dbg(dev, "Alarm IRQ armed\n");
	}
	dev_dbg(dev, "Set alarm: %ptRdr(%d) %ptRt\n",
		&alm->time, alm->time.tm_wday, &alm->time);
	return 0;
}

static int mcp795_read_alarm(struct device *dev, struct rtc_wkalrm *alm)
{
	u8 data[6];
	int ret;

	ret = mcp795_rtcc_read(
			dev, MCP795_REG_ALM0_SECONDS, data, sizeof(data));
	if (ret)
		return ret;

	alm->time.tm_sec	= bcd2bin(data[0] & 0x7F);
	alm->time.tm_min	= bcd2bin(data[1] & 0x7F);
	alm->time.tm_hour	= bcd2bin(data[2] & 0x1F);
	alm->time.tm_wday	= bcd2bin(data[3] & 0x07) - 1;
	alm->time.tm_mday	= bcd2bin(data[4] & 0x3F);
	alm->time.tm_mon	= bcd2bin(data[5] & 0x1F) - 1;
	alm->time.tm_year	= -1;
	alm->time.tm_isdst	= -1;
	alm->time.tm_yday	= -1;

	dev_dbg(dev, "Read alarm: %ptRdr(%d) %ptRt\n",
		&alm->time, alm->time.tm_wday, &alm->time);
	return 0;
}

static int mcp795_alarm_irq_enable(struct device *dev, unsigned int enabled)
{
	return mcp795_update_alarm(dev, !!enabled);
}

static irqreturn_t mcp795_irq(int irq, void *data)
{
	struct spi_device *spi = data;
	struct rtc_device *rtc = spi_get_drvdata(spi);
	struct mutex *lock = &rtc->ops_lock;
	int ret;

	mutex_lock(lock);

	/* Disable alarm.
	 * There is no need to clear ALM0IF (Alarm 0 Interrupt Flag) bit,
	 * because it is done every time when alarm is enabled.
	 */
	ret = mcp795_update_alarm(&spi->dev, false);
	if (ret)
		dev_err(&spi->dev,
			"Failed to disable alarm in IRQ (ret=%d)\n", ret);
	rtc_update_irq(rtc, 1, RTC_AF | RTC_IRQF);

	mutex_unlock(lock);

	return IRQ_HANDLED;
}

static const struct rtc_class_ops mcp795_rtc_ops = {
		.read_time = mcp795_read_time,
		.set_time = mcp795_set_time,
		.read_alarm = mcp795_read_alarm,
		.set_alarm = mcp795_set_alarm,
		.alarm_irq_enable = mcp795_alarm_irq_enable
};

static int mcp795_probe(struct spi_device *spi)
{
	struct rtc_device *rtc;
	int ret;

	spi->mode = SPI_MODE_0;
	spi->bits_per_word = 8;
	ret = spi_setup(spi);
	if (ret) {
		dev_err(&spi->dev, "Unable to setup SPI\n");
		return ret;
	}

	/* Start the oscillator but don't set the value of EXTOSC bit */
	mcp795_start_oscillator(&spi->dev, NULL);
	/* Clear the 12 hour mode flag*/
	mcp795_rtcc_set_bits(&spi->dev, 0x03, MCP795_24_BIT, 0);

	rtc = devm_rtc_device_register(&spi->dev, "rtc-mcp795",
					&mcp795_rtc_ops, THIS_MODULE);
	if (IS_ERR(rtc))
		return PTR_ERR(rtc);

	spi_set_drvdata(spi, rtc);

	if (spi->irq > 0) {
		dev_dbg(&spi->dev, "Alarm support enabled\n");

		/* Clear any pending alarm (ALM0IF bit) before requesting
		 * the interrupt.
		 */
		mcp795_rtcc_set_bits(&spi->dev, MCP795_REG_ALM0_DAY,
					MCP795_ALM0IF_BIT, 0);
		ret = devm_request_threaded_irq(&spi->dev, spi->irq, NULL,
				mcp795_irq, IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
				dev_name(&rtc->dev), spi);
		if (ret)
			dev_err(&spi->dev, "Failed to request IRQ: %d: %d\n",
						spi->irq, ret);
		else
			device_init_wakeup(&spi->dev, true);
	}
	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id mcp795_of_match[] = {
	{ .compatible = "maxim,mcp795" },
	{ }
};
MODULE_DEVICE_TABLE(of, mcp795_of_match);
#endif

static struct spi_driver mcp795_driver = {
		.driver = {
				.name = "rtc-mcp795",
				.of_match_table = of_match_ptr(mcp795_of_match),
		},
		.probe = mcp795_probe,
};

module_spi_driver(mcp795_driver);

MODULE_DESCRIPTION("MCP795 RTC SPI Driver");
MODULE_AUTHOR("Josef Gajdusek <atx@atx.name>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("spi:mcp795");
