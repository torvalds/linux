// SPDX-License-Identifier: GPL-2.0
/*
 * Real Time Clock (RTC) Driver for sd3078
 * Copyright (C) 2018 Zoro Li
 */

#include <linux/bcd.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/rtc.h>
#include <linux/slab.h>

#define SD3078_REG_SC			0x00
#define SD3078_REG_MN			0x01
#define SD3078_REG_HR			0x02
#define SD3078_REG_DW			0x03
#define SD3078_REG_DM			0x04
#define SD3078_REG_MO			0x05
#define SD3078_REG_YR			0x06

#define SD3078_REG_CTRL1		0x0f
#define SD3078_REG_CTRL2		0x10
#define SD3078_REG_CTRL3		0x11

#define KEY_WRITE1		0x80
#define KEY_WRITE2		0x04
#define KEY_WRITE3		0x80

#define NUM_TIME_REGS   (SD3078_REG_YR - SD3078_REG_SC + 1)

/*
 * The sd3078 has write protection
 * and we can choose whether or not to use it.
 * Write protection is turned off by default.
 */
#define WRITE_PROTECT_EN	0

struct sd3078 {
	struct rtc_device	*rtc;
	struct regmap		*regmap;
};

/*
 * In order to prevent arbitrary modification of the time register,
 * when modification of the register,
 * the "write" bit needs to be written in a certain order.
 * 1. set WRITE1 bit
 * 2. set WRITE2 bit
 * 3. set WRITE3 bit
 */
static void sd3078_enable_reg_write(struct sd3078 *sd3078)
{
	regmap_update_bits(sd3078->regmap, SD3078_REG_CTRL2,
			   KEY_WRITE1, KEY_WRITE1);
	regmap_update_bits(sd3078->regmap, SD3078_REG_CTRL1,
			   KEY_WRITE2, KEY_WRITE2);
	regmap_update_bits(sd3078->regmap, SD3078_REG_CTRL1,
			   KEY_WRITE3, KEY_WRITE3);
}

#if WRITE_PROTECT_EN
/*
 * In order to prevent arbitrary modification of the time register,
 * we should disable the write function.
 * when disable write,
 * the "write" bit needs to be clear in a certain order.
 * 1. clear WRITE2 bit
 * 2. clear WRITE3 bit
 * 3. clear WRITE1 bit
 */
static void sd3078_disable_reg_write(struct sd3078 *sd3078)
{
	regmap_update_bits(sd3078->regmap, SD3078_REG_CTRL1,
			   KEY_WRITE2, 0);
	regmap_update_bits(sd3078->regmap, SD3078_REG_CTRL1,
			   KEY_WRITE3, 0);
	regmap_update_bits(sd3078->regmap, SD3078_REG_CTRL2,
			   KEY_WRITE1, 0);
}
#endif

static int sd3078_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
	unsigned char hour;
	unsigned char rtc_data[NUM_TIME_REGS] = {0};
	struct i2c_client *client = to_i2c_client(dev);
	struct sd3078 *sd3078 = i2c_get_clientdata(client);
	int ret;

	ret = regmap_bulk_read(sd3078->regmap, SD3078_REG_SC, rtc_data,
			       NUM_TIME_REGS);
	if (ret < 0) {
		dev_err(dev, "reading from RTC failed with err:%d\n", ret);
		return ret;
	}

	tm->tm_sec	= bcd2bin(rtc_data[SD3078_REG_SC] & 0x7F);
	tm->tm_min	= bcd2bin(rtc_data[SD3078_REG_MN] & 0x7F);

	/*
	 * The sd3078 supports 12/24 hour mode.
	 * When getting time,
	 * we need to convert the 12 hour mode to the 24 hour mode.
	 */
	hour = rtc_data[SD3078_REG_HR];
	if (hour & 0x80) /* 24H MODE */
		tm->tm_hour = bcd2bin(rtc_data[SD3078_REG_HR] & 0x3F);
	else if (hour & 0x20) /* 12H MODE PM */
		tm->tm_hour = bcd2bin(rtc_data[SD3078_REG_HR] & 0x1F) + 12;
	else /* 12H MODE AM */
		tm->tm_hour = bcd2bin(rtc_data[SD3078_REG_HR] & 0x1F);

	tm->tm_mday = bcd2bin(rtc_data[SD3078_REG_DM] & 0x3F);
	tm->tm_wday = rtc_data[SD3078_REG_DW] & 0x07;
	tm->tm_mon	= bcd2bin(rtc_data[SD3078_REG_MO] & 0x1F) - 1;
	tm->tm_year = bcd2bin(rtc_data[SD3078_REG_YR]) + 100;

	return 0;
}

static int sd3078_rtc_set_time(struct device *dev, struct rtc_time *tm)
{
	unsigned char rtc_data[NUM_TIME_REGS];
	struct i2c_client *client = to_i2c_client(dev);
	struct sd3078 *sd3078 = i2c_get_clientdata(client);
	int ret;

	rtc_data[SD3078_REG_SC] = bin2bcd(tm->tm_sec);
	rtc_data[SD3078_REG_MN] = bin2bcd(tm->tm_min);
	rtc_data[SD3078_REG_HR] = bin2bcd(tm->tm_hour) | 0x80;
	rtc_data[SD3078_REG_DM] = bin2bcd(tm->tm_mday);
	rtc_data[SD3078_REG_DW] = tm->tm_wday & 0x07;
	rtc_data[SD3078_REG_MO] = bin2bcd(tm->tm_mon) + 1;
	rtc_data[SD3078_REG_YR] = bin2bcd(tm->tm_year - 100);

#if WRITE_PROTECT_EN
	sd3078_enable_reg_write(sd3078);
#endif

	ret = regmap_bulk_write(sd3078->regmap, SD3078_REG_SC, rtc_data,
				NUM_TIME_REGS);
	if (ret < 0) {
		dev_err(dev, "writing to RTC failed with err:%d\n", ret);
		return ret;
	}

#if WRITE_PROTECT_EN
	sd3078_disable_reg_write(sd3078);
#endif

	return 0;
}

static const struct rtc_class_ops sd3078_rtc_ops = {
	.read_time	= sd3078_rtc_read_time,
	.set_time	= sd3078_rtc_set_time,
};

static const struct regmap_config regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = 0x11,
};

static int sd3078_probe(struct i2c_client *client)
{
	int ret;
	struct sd3078 *sd3078;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C))
		return -ENODEV;

	sd3078 = devm_kzalloc(&client->dev, sizeof(*sd3078), GFP_KERNEL);
	if (!sd3078)
		return -ENOMEM;

	sd3078->regmap = devm_regmap_init_i2c(client, &regmap_config);
	if (IS_ERR(sd3078->regmap)) {
		dev_err(&client->dev, "regmap allocation failed\n");
		return PTR_ERR(sd3078->regmap);
	}

	i2c_set_clientdata(client, sd3078);

	sd3078->rtc = devm_rtc_allocate_device(&client->dev);
	if (IS_ERR(sd3078->rtc))
		return PTR_ERR(sd3078->rtc);

	sd3078->rtc->ops = &sd3078_rtc_ops;
	sd3078->rtc->range_min = RTC_TIMESTAMP_BEGIN_2000;
	sd3078->rtc->range_max = RTC_TIMESTAMP_END_2099;

	ret = devm_rtc_register_device(sd3078->rtc);
	if (ret)
		return ret;

	sd3078_enable_reg_write(sd3078);

	return 0;
}

static const struct i2c_device_id sd3078_id[] = {
	{"sd3078", 0},
	{ }
};
MODULE_DEVICE_TABLE(i2c, sd3078_id);

static const __maybe_unused struct of_device_id rtc_dt_match[] = {
	{ .compatible = "whwave,sd3078" },
	{},
};
MODULE_DEVICE_TABLE(of, rtc_dt_match);

static struct i2c_driver sd3078_driver = {
	.driver     = {
		.name   = "sd3078",
		.of_match_table = of_match_ptr(rtc_dt_match),
	},
	.probe      = sd3078_probe,
	.id_table   = sd3078_id,
};

module_i2c_driver(sd3078_driver);

MODULE_AUTHOR("Dianlong Li <long17.cool@163.com>");
MODULE_DESCRIPTION("SD3078 RTC driver");
MODULE_LICENSE("GPL v2");
