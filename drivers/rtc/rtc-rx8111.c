// SPDX-License-Identifier: GPL-2.0-only
/*
 * Driver for Epson RX8111 RTC.
 *
 * Copyright (C) 2023 Axis Communications AB
 */

#include <linux/bcd.h>
#include <linux/bitfield.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/regmap.h>

#include <linux/rtc.h>

#define RX8111_REG_SEC			0x10	/* Second counter. */
#define RX8111_REG_MIN			0x11	/* Minute counter */
#define RX8111_REG_HOUR			0x12	/* Hour counter. */
#define RX8111_REG_WEEK			0x13	/* Week day counter. */
#define RX8111_REG_DAY			0x14	/* Month day counter. */
#define RX8111_REG_MONTH		0x15	/* Month counter. */
#define RX8111_REG_YEAR			0x16	/* Year counter. */

#define RX8111_REG_ALARM_MIN		0x17	/* Alarm minute. */
#define RX8111_REG_ALARM_HOUR		0x18	/* Alarm hour. */
#define RX8111_REG_ALARM_WEEK_DAY	0x19	/* Alarm week or month day. */

#define RX8111_REG_TIMER_COUNTER0	0x1a	/* Timer counter LSB. */
#define RX8111_REG_TIMER_COUNTER1	0x1b	/* Timer counter. */
#define RX8111_REG_TIMER_COUNTER2	0x1c	/* Timer counter MSB. */

#define RX8111_REG_EXT			0x1d	/* Extension register. */
#define RX8111_REG_FLAG			0x1e	/* Flag register. */
#define RX8111_REG_CTRL			0x1f	/* Control register. */

#define RX8111_REG_TS_1_1000_SEC	0x20	/* Timestamp 256 or 512 Hz . */
#define RX8111_REG_TS_1_100_SEC		0x21	/* Timestamp 1 - 128 Hz. */
#define RX8111_REG_TS_SEC		0x22	/* Timestamp second. */
#define RX8111_REG_TS_MIN		0x23	/* Timestamp minute. */
#define RX8111_REG_TS_HOUR		0x24	/* Timestamp hour. */
#define RX8111_REG_TS_WEEK		0x25	/* Timestamp week day. */
#define RX8111_REG_TS_DAY		0x26	/* Timestamp month day. */
#define RX8111_REG_TS_MONTH		0x27	/* Timestamp month. */
#define RX8111_REG_TS_YEAR		0x28	/* Timestamp year. */
#define RX8111_REG_TS_STATUS		0x29	/* Timestamp status. */

#define RX8111_REG_EVIN_SETTING		0x2b	/* Timestamp trigger setting. */
#define RX8111_REG_ALARM_SEC		0x2c	/* Alarm second. */
#define RX8111_REG_TIMER_CTRL		0x2d	/* Timer control. */
#define RX8111_REG_TS_CTRL0		0x2e	/* Timestamp control 0. */
#define RX8111_REG_CMD_TRIGGER		0x2f	/* Timestamp trigger. */
#define RX8111_REG_PWR_SWITCH_CTRL	0x32	/* Power switch control. */
#define RX8111_REG_STATUS_MON		0x33	/* Status monitor. */
#define RX8111_REG_TS_CTRL1		0x34	/* Timestamp control 1. */
#define RX8111_REG_TS_CTRL2		0x35	/* Timestamp control 2. */
#define RX8111_REG_TS_CTRL3		0x36	/* Timestamp control 3. */

#define RX8111_FLAG_XST_BIT BIT(0)
#define RX8111_FLAG_VLF_BIT BIT(1)

#define RX8111_TIME_BUF_SZ (RX8111_REG_YEAR - RX8111_REG_SEC + 1)

enum rx8111_regfield {
	/* RX8111_REG_EXT. */
	RX8111_REGF_TSEL0,
	RX8111_REGF_TSEL1,
	RX8111_REGF_ETS,
	RX8111_REGF_WADA,
	RX8111_REGF_TE,
	RX8111_REGF_USEL,
	RX8111_REGF_FSEL0,
	RX8111_REGF_FSEL1,

	/* RX8111_REG_FLAG. */
	RX8111_REGF_XST,
	RX8111_REGF_VLF,
	RX8111_REGF_EVF,
	RX8111_REGF_AF,
	RX8111_REGF_TF,
	RX8111_REGF_UF,
	RX8111_REGF_POR,

	/* RX8111_REG_CTRL. */
	RX8111_REGF_STOP,
	RX8111_REGF_EIE,
	RX8111_REGF_AIE,
	RX8111_REGF_TIE,
	RX8111_REGF_UIE,

	/* RX8111_REG_PWR_SWITCH_CTRL. */
	RX8111_REGF_SMPT0,
	RX8111_REGF_SMPT1,
	RX8111_REGF_SWSEL0,
	RX8111_REGF_SWSEL1,
	RX8111_REGF_INIEN,
	RX8111_REGF_CHGEN,

	/* RX8111_REG_STATUS_MON. */
	RX8111_REGF_VLOW,

	/* Sentinel value. */
	RX8111_REGF_MAX
};

static const struct reg_field rx8111_regfields[] = {
	[RX8111_REGF_TSEL0] = REG_FIELD(RX8111_REG_EXT, 0, 0),
	[RX8111_REGF_TSEL1] = REG_FIELD(RX8111_REG_EXT, 1, 1),
	[RX8111_REGF_ETS]   = REG_FIELD(RX8111_REG_EXT, 2, 2),
	[RX8111_REGF_WADA]  = REG_FIELD(RX8111_REG_EXT, 3, 3),
	[RX8111_REGF_TE]    = REG_FIELD(RX8111_REG_EXT, 4, 4),
	[RX8111_REGF_USEL]  = REG_FIELD(RX8111_REG_EXT, 5, 5),
	[RX8111_REGF_FSEL0] = REG_FIELD(RX8111_REG_EXT, 6, 6),
	[RX8111_REGF_FSEL1] = REG_FIELD(RX8111_REG_EXT, 7, 7),

	[RX8111_REGF_XST] = REG_FIELD(RX8111_REG_FLAG, 0, 0),
	[RX8111_REGF_VLF] = REG_FIELD(RX8111_REG_FLAG, 1, 1),
	[RX8111_REGF_EVF] = REG_FIELD(RX8111_REG_FLAG, 2, 2),
	[RX8111_REGF_AF]  = REG_FIELD(RX8111_REG_FLAG, 3, 3),
	[RX8111_REGF_TF]  = REG_FIELD(RX8111_REG_FLAG, 4, 4),
	[RX8111_REGF_UF]  = REG_FIELD(RX8111_REG_FLAG, 5, 5),
	[RX8111_REGF_POR] = REG_FIELD(RX8111_REG_FLAG, 7, 7),

	[RX8111_REGF_STOP] = REG_FIELD(RX8111_REG_CTRL, 0, 0),
	[RX8111_REGF_EIE]  = REG_FIELD(RX8111_REG_CTRL, 2, 2),
	[RX8111_REGF_AIE]  = REG_FIELD(RX8111_REG_CTRL, 3, 3),
	[RX8111_REGF_TIE]  = REG_FIELD(RX8111_REG_CTRL, 4, 4),
	[RX8111_REGF_UIE]  = REG_FIELD(RX8111_REG_CTRL, 5, 5),

	[RX8111_REGF_SMPT0]  = REG_FIELD(RX8111_REG_PWR_SWITCH_CTRL, 0, 0),
	[RX8111_REGF_SMPT1]  = REG_FIELD(RX8111_REG_PWR_SWITCH_CTRL, 1, 1),
	[RX8111_REGF_SWSEL0] = REG_FIELD(RX8111_REG_PWR_SWITCH_CTRL, 2, 2),
	[RX8111_REGF_SWSEL1] = REG_FIELD(RX8111_REG_PWR_SWITCH_CTRL, 3, 3),
	[RX8111_REGF_INIEN]  = REG_FIELD(RX8111_REG_PWR_SWITCH_CTRL, 6, 6),
	[RX8111_REGF_CHGEN]  = REG_FIELD(RX8111_REG_PWR_SWITCH_CTRL, 7, 7),

	[RX8111_REGF_VLOW]  = REG_FIELD(RX8111_REG_STATUS_MON, 1, 1),
};

static const struct regmap_config rx8111_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = RX8111_REG_TS_CTRL3,
};

struct rx8111_data {
	struct regmap *regmap;
	struct regmap_field *regfields[RX8111_REGF_MAX];
	struct device *dev;
	struct rtc_device *rtc;
};

static int rx8111_read_vl_flag(struct rx8111_data *data, unsigned int *vlval)
{
	int ret;

	ret = regmap_field_read(data->regfields[RX8111_REGF_VLF], vlval);
	if (ret)
		dev_dbg(data->dev, "Could not read VL flag (%d)", ret);

	return ret;
}

static int rx8111_read_time(struct device *dev, struct rtc_time *tm)
{
	struct rx8111_data *data = dev_get_drvdata(dev);
	u8 buf[RX8111_TIME_BUF_SZ];
	unsigned int regval;
	int ret;

	/* Check status. */
	ret = regmap_read(data->regmap, RX8111_REG_FLAG, &regval);
	if (ret) {
		dev_dbg(data->dev, "Could not read flag register (%d)\n", ret);
		return ret;
	}

	if (FIELD_GET(RX8111_FLAG_XST_BIT, regval)) {
		dev_dbg(data->dev,
			"Crystal oscillation stopped, time is not reliable\n");
		return -EINVAL;
	}

	if (FIELD_GET(RX8111_FLAG_VLF_BIT, regval)) {
		dev_dbg(data->dev,
			"Low voltage detected, time is not reliable\n");
		return -EINVAL;
	}

	ret = regmap_field_read(data->regfields[RX8111_REGF_STOP], &regval);
	if (ret) {
		dev_dbg(data->dev, "Could not read clock status (%d)\n", ret);
		return ret;
	}

	if (regval) {
		dev_dbg(data->dev, "Clock stopped, time is not reliable\n");
		return -EINVAL;
	}

	/* Read time. */
	ret = regmap_bulk_read(data->regmap, RX8111_REG_SEC, buf,
			       ARRAY_SIZE(buf));
	if (ret) {
		dev_dbg(data->dev, "Could not bulk read time (%d)\n", ret);
		return ret;
	}

	tm->tm_sec = bcd2bin(buf[0]);
	tm->tm_min = bcd2bin(buf[1]);
	tm->tm_hour = bcd2bin(buf[2]);
	tm->tm_wday = ffs(buf[3]) - 1;
	tm->tm_mday = bcd2bin(buf[4]);
	tm->tm_mon = bcd2bin(buf[5]) - 1;
	tm->tm_year = bcd2bin(buf[6]) + 100;

	return 0;
}

static int rx8111_set_time(struct device *dev, struct rtc_time *tm)
{
	struct rx8111_data *data = dev_get_drvdata(dev);
	u8 buf[RX8111_TIME_BUF_SZ];
	int ret;

	buf[0] = bin2bcd(tm->tm_sec);
	buf[1] = bin2bcd(tm->tm_min);
	buf[2] = bin2bcd(tm->tm_hour);
	buf[3] = BIT(tm->tm_wday);
	buf[4] = bin2bcd(tm->tm_mday);
	buf[5] = bin2bcd(tm->tm_mon + 1);
	buf[6] = bin2bcd(tm->tm_year - 100);

	ret = regmap_clear_bits(data->regmap, RX8111_REG_FLAG,
				RX8111_FLAG_XST_BIT | RX8111_FLAG_VLF_BIT);
	if (ret)
		return ret;

	/* Stop the clock. */
	ret = regmap_field_write(data->regfields[RX8111_REGF_STOP], 1);
	if (ret) {
		dev_dbg(data->dev, "Could not stop the clock (%d)\n", ret);
		return ret;
	}

	/* Set the time. */
	ret = regmap_bulk_write(data->regmap, RX8111_REG_SEC, buf,
				ARRAY_SIZE(buf));
	if (ret) {
		dev_dbg(data->dev, "Could not bulk write time (%d)\n", ret);

		/*
		 * We don't bother with trying to start the clock again. We
		 * check for this in rx8111_read_time() (and thus force user to
		 * call rx8111_set_time() to try again).
		 */
		return ret;
	}

	/* Start the clock. */
	ret = regmap_field_write(data->regfields[RX8111_REGF_STOP], 0);
	if (ret) {
		dev_dbg(data->dev, "Could not start the clock (%d)\n", ret);
		return ret;
	}

	return 0;
}

static int rx8111_ioctl(struct device *dev, unsigned int cmd, unsigned long arg)
{
	struct rx8111_data *data = dev_get_drvdata(dev);
	unsigned int regval;
	unsigned int vlval;
	int ret;

	switch (cmd) {
	case RTC_VL_READ:
		ret = rx8111_read_vl_flag(data, &regval);
		if (ret)
			return ret;

		vlval = regval ? RTC_VL_DATA_INVALID : 0;

		ret = regmap_field_read(data->regfields[RX8111_REGF_VLOW],
					&regval);
		if (ret)
			return ret;

		vlval |= regval ? RTC_VL_BACKUP_LOW : 0;

		return put_user(vlval, (typeof(vlval) __user *)arg);
	default:
		return -ENOIOCTLCMD;
	}
}

static const struct rtc_class_ops rx8111_rtc_ops = {
	.read_time = rx8111_read_time,
	.set_time = rx8111_set_time,
	.ioctl = rx8111_ioctl,
};

static int rx8111_probe(struct i2c_client *client)
{
	struct rx8111_data *data;
	struct rtc_device *rtc;
	size_t i;

	data = devm_kmalloc(&client->dev, sizeof(*data), GFP_KERNEL);
	if (!data) {
		dev_dbg(&client->dev, "Could not allocate device data\n");
		return -ENOMEM;
	}

	data->dev = &client->dev;
	dev_set_drvdata(data->dev, data);

	data->regmap = devm_regmap_init_i2c(client, &rx8111_regmap_config);
	if (IS_ERR(data->regmap)) {
		dev_dbg(data->dev, "Could not initialize regmap\n");
		return PTR_ERR(data->regmap);
	}

	for (i = 0; i < RX8111_REGF_MAX; ++i) {
		data->regfields[i] = devm_regmap_field_alloc(
			data->dev, data->regmap, rx8111_regfields[i]);
		if (IS_ERR(data->regfields[i])) {
			dev_dbg(data->dev,
				"Could not allocate register field %zu\n", i);
			return PTR_ERR(data->regfields[i]);
		}
	}

	rtc = devm_rtc_allocate_device(data->dev);
	if (IS_ERR(rtc)) {
		dev_dbg(data->dev, "Could not allocate rtc device\n");
		return PTR_ERR(rtc);
	}

	rtc->ops = &rx8111_rtc_ops;
	rtc->range_min = RTC_TIMESTAMP_BEGIN_2000;
	rtc->range_max = RTC_TIMESTAMP_END_2099;

	clear_bit(RTC_FEATURE_ALARM, rtc->features);

	return devm_rtc_register_device(rtc);
}

static const struct of_device_id rx8111_of_match[] = {
	{
		.compatible = "epson,rx8111",
	},
	{}
};
MODULE_DEVICE_TABLE(of, rx8111_of_match);

static struct i2c_driver rx8111_driver = {
	.driver = {
		.name = "rtc-rx8111",
		.of_match_table = rx8111_of_match,
	},
	.probe = rx8111_probe,
};
module_i2c_driver(rx8111_driver);

MODULE_AUTHOR("Waqar Hameed <waqar.hameed@axis.com>");
MODULE_DESCRIPTION("Epson RX8111 RTC driver");
MODULE_LICENSE("GPL");
