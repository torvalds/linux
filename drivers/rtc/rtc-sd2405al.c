// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * RTC driver for the SD2405AL Real-Time Clock
 *
 * Datasheet:
 * https://image.dfrobot.com/image/data/TOY0021/SD2405AL%20datasheet%20(Angelo%20v0.1).pdf
 *
 * I2C slave address: 0x32
 *
 * Copyright (C) 2024-2025 Tóth János <gomba007@gmail.com>
 */

#include <linux/bcd.h>
#include <linux/i2c.h>
#include <linux/regmap.h>
#include <linux/rtc.h>

/* Real time clock registers */
#define SD2405AL_REG_T_SEC	0x00
#define SD2405AL_REG_T_MIN	0x01
#define SD2405AL_REG_T_HOUR	0x02
#	define SD2405AL_BIT_12H_PM	BIT(5)
#	define SD2405AL_BIT_24H		BIT(7)
#define SD2405AL_REG_T_WEEK	0x03
#define SD2405AL_REG_T_DAY	0x04
#define SD2405AL_REG_T_MON	0x05
#define SD2405AL_REG_T_YEAR	0x06

#define SD2405AL_NUM_T_REGS	(SD2405AL_REG_T_YEAR - SD2405AL_REG_T_SEC + 1)

/* Control registers */
#define SD2405AL_REG_CTR1	0x0F
#	define SD2405AL_BIT_WRTC2	BIT(2)
#	define SD2405AL_BIT_WRTC3	BIT(7)
#define SD2405AL_REG_CTR2	0x10
#	define SD2405AL_BIT_WRTC1	BIT(7)
#define SD2405AL_REG_CTR3	0x11
#define SD2405AL_REG_TTF	0x12
#define SD2405AL_REG_CNTDWN	0x13

/* General RAM */
#define SD2405AL_REG_M_START	0x14
#define SD2405AL_REG_M_END	0x1F

struct sd2405al {
	struct device		*dev;
	struct regmap		*regmap;
};

static int sd2405al_enable_reg_write(struct sd2405al *sd2405al)
{
	int ret;

	/* order of writes is important */
	ret = regmap_update_bits(sd2405al->regmap, SD2405AL_REG_CTR2,
				 SD2405AL_BIT_WRTC1, SD2405AL_BIT_WRTC1);
	if (ret < 0)
		return ret;

	ret = regmap_update_bits(sd2405al->regmap, SD2405AL_REG_CTR1,
				 SD2405AL_BIT_WRTC2 | SD2405AL_BIT_WRTC3,
				 SD2405AL_BIT_WRTC2 | SD2405AL_BIT_WRTC3);
	if (ret < 0)
		return ret;

	return 0;
}

static int sd2405al_disable_reg_write(struct sd2405al *sd2405al)
{
	int ret;

	/* order of writes is important */
	ret = regmap_update_bits(sd2405al->regmap, SD2405AL_REG_CTR1,
				 SD2405AL_BIT_WRTC2 | SD2405AL_BIT_WRTC3, 0x00);
	if (ret < 0)
		return ret;

	ret = regmap_update_bits(sd2405al->regmap, SD2405AL_REG_CTR2,
				 SD2405AL_BIT_WRTC1, 0x00);
	if (ret < 0)
		return ret;

	return 0;
}

static int sd2405al_read_time(struct device *dev, struct rtc_time *time)
{
	u8 data[SD2405AL_NUM_T_REGS] = { 0 };
	struct sd2405al *sd2405al = dev_get_drvdata(dev);
	int ret;

	ret = regmap_bulk_read(sd2405al->regmap, SD2405AL_REG_T_SEC, data,
			       SD2405AL_NUM_T_REGS);
	if (ret < 0)
		return ret;

	time->tm_sec = bcd2bin(data[SD2405AL_REG_T_SEC] & 0x7F);
	time->tm_min = bcd2bin(data[SD2405AL_REG_T_MIN] & 0x7F);

	if (data[SD2405AL_REG_T_HOUR] & SD2405AL_BIT_24H)
		time->tm_hour = bcd2bin(data[SD2405AL_REG_T_HOUR] & 0x3F);
	else
		if (data[SD2405AL_REG_T_HOUR] & SD2405AL_BIT_12H_PM)
			time->tm_hour = bcd2bin(data[SD2405AL_REG_T_HOUR]
						& 0x1F) + 12;
		else /* 12 hour mode, AM */
			time->tm_hour = bcd2bin(data[SD2405AL_REG_T_HOUR]
						& 0x1F);

	time->tm_wday = bcd2bin(data[SD2405AL_REG_T_WEEK] & 0x07);
	time->tm_mday = bcd2bin(data[SD2405AL_REG_T_DAY] & 0x3F);
	time->tm_mon = bcd2bin(data[SD2405AL_REG_T_MON] & 0x1F) - 1;
	time->tm_year = bcd2bin(data[SD2405AL_REG_T_YEAR]) + 100;

	dev_dbg(sd2405al->dev, "read time: %ptR (%d)\n", time, time->tm_wday);

	return 0;
}

static int sd2405al_set_time(struct device *dev, struct rtc_time *time)
{
	u8 data[SD2405AL_NUM_T_REGS];
	struct sd2405al *sd2405al = dev_get_drvdata(dev);
	int ret;

	data[SD2405AL_REG_T_SEC] = bin2bcd(time->tm_sec);
	data[SD2405AL_REG_T_MIN] = bin2bcd(time->tm_min);
	data[SD2405AL_REG_T_HOUR] = bin2bcd(time->tm_hour) | SD2405AL_BIT_24H;
	data[SD2405AL_REG_T_DAY] = bin2bcd(time->tm_mday);
	data[SD2405AL_REG_T_WEEK] = bin2bcd(time->tm_wday);
	data[SD2405AL_REG_T_MON] = bin2bcd(time->tm_mon) + 1;
	data[SD2405AL_REG_T_YEAR] = bin2bcd(time->tm_year - 100);

	ret = sd2405al_enable_reg_write(sd2405al);
	if (ret < 0)
		return ret;

	ret = regmap_bulk_write(sd2405al->regmap, SD2405AL_REG_T_SEC, data,
				SD2405AL_NUM_T_REGS);
	if (ret < 0)
		return ret;

	ret = regmap_write(sd2405al->regmap, SD2405AL_REG_TTF, 0x00);
	if (ret < 0)
		return ret;

	ret = sd2405al_disable_reg_write(sd2405al);
	if (ret < 0)
		return ret;

	dev_dbg(sd2405al->dev, "set time: %ptR (%d)\n", time, time->tm_wday);

	return 0;
}

static const struct rtc_class_ops sd2405al_rtc_ops = {
	.read_time = sd2405al_read_time,
	.set_time = sd2405al_set_time,
};

static const struct regmap_config sd2405al_regmap_conf = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = SD2405AL_REG_M_END,
};

static int sd2405al_probe(struct i2c_client *client)
{
	struct sd2405al *sd2405al;
	struct rtc_device *rtc;
	int ret;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C))
		return -ENODEV;

	sd2405al = devm_kzalloc(&client->dev, sizeof(*sd2405al), GFP_KERNEL);
	if (!sd2405al)
		return -ENOMEM;

	sd2405al->dev = &client->dev;

	sd2405al->regmap = devm_regmap_init_i2c(client, &sd2405al_regmap_conf);
	if (IS_ERR(sd2405al->regmap))
		return PTR_ERR(sd2405al->regmap);

	rtc = devm_rtc_allocate_device(&client->dev);
	if (IS_ERR(rtc))
		return PTR_ERR(rtc);

	rtc->ops = &sd2405al_rtc_ops;
	rtc->range_min = RTC_TIMESTAMP_BEGIN_2000;
	rtc->range_max = RTC_TIMESTAMP_END_2099;

	dev_set_drvdata(&client->dev, sd2405al);

	ret = devm_rtc_register_device(rtc);
	if (ret < 0)
		return ret;

	return 0;
}

static const struct i2c_device_id sd2405al_id[] = {
	{ "sd2405al" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(i2c, sd2405al_id);

static const __maybe_unused struct of_device_id sd2405al_of_match[] = {
	{ .compatible = "dfrobot,sd2405al" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, sd2405al_of_match);

static struct i2c_driver sd2405al_driver = {
	.driver = {
		.name = "sd2405al",
		.of_match_table = of_match_ptr(sd2405al_of_match),
	},
	.probe = sd2405al_probe,
	.id_table = sd2405al_id,
};

module_i2c_driver(sd2405al_driver);

MODULE_AUTHOR("Tóth János <gomba007@gmail.com>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("SD2405AL RTC driver");
