// SPDX-License-Identifier: GPL-2.0-only
#include <linux/limits.h>
#include <linux/mod_devicetable.h>
#include <linux/platform_device.h>
#include <linux/rtc.h>

#include <linux/mfd/88pm886.h>

/*
 * Time is calculated as the sum of a 32-bit read-only advancing counter and a
 * writeable constant offset stored in the chip's spare registers.
 */

static int pm886_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
	struct regmap *regmap = dev_get_drvdata(dev);
	u32 time;
	u32 buf;
	int ret;

	ret = regmap_bulk_read(regmap, PM886_REG_RTC_SPARE1, &buf, 4);
	if (ret)
		return ret;
	time = buf;

	ret = regmap_bulk_read(regmap, PM886_REG_RTC_CNT1, &buf, 4);
	if (ret)
		return ret;
	time += buf;

	rtc_time64_to_tm(time, tm);

	return 0;
}

static int pm886_rtc_set_time(struct device *dev, struct rtc_time *tm)
{
	struct regmap *regmap = dev_get_drvdata(dev);
	u32 buf;
	int ret;

	ret = regmap_bulk_read(regmap, PM886_REG_RTC_CNT1, &buf, 4);
	if (ret)
		return ret;

	buf = rtc_tm_to_time64(tm) - buf;

	return regmap_bulk_write(regmap, PM886_REG_RTC_SPARE1, &buf, 4);
}

static const struct rtc_class_ops pm886_rtc_ops = {
	.read_time = pm886_rtc_read_time,
	.set_time = pm886_rtc_set_time,
};

static int pm886_rtc_probe(struct platform_device *pdev)
{
	struct pm886_chip *chip = dev_get_drvdata(pdev->dev.parent);
	struct device *dev = &pdev->dev;
	struct rtc_device *rtc;
	int ret;

	platform_set_drvdata(pdev, chip->regmap);

	rtc = devm_rtc_allocate_device(dev);
	if (IS_ERR(rtc))
		return dev_err_probe(dev, PTR_ERR(rtc),
				"Failed to allocate RTC device\n");

	rtc->ops = &pm886_rtc_ops;
	rtc->range_max = U32_MAX;

	ret = devm_rtc_register_device(rtc);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to register RTC device\n");

	return 0;
}

static const struct platform_device_id pm886_rtc_id_table[] = {
	{ "88pm886-rtc", },
	{ }
};
MODULE_DEVICE_TABLE(platform, pm886_rtc_id_table);

static struct platform_driver pm886_rtc_driver = {
	.driver = {
		.name = "88pm886-rtc",
	},
	.probe = pm886_rtc_probe,
	.id_table = pm886_rtc_id_table,
};
module_platform_driver(pm886_rtc_driver);

MODULE_DESCRIPTION("Marvell 88PM886 RTC driver");
MODULE_AUTHOR("Karel Balej <balejk@matfyz.cz>");
MODULE_LICENSE("GPL");
