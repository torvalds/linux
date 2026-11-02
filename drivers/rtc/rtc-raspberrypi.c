// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/**
 * rtc-raspberrypi.c
 *
 * RTC driver using firmware mailbox
 * Supports battery backed RTC and wake alarms
 *
 * Based on rtc-meson-vrtc by Neil Armstrong
 *
 * Copyright (c) 2023, Raspberry Pi Ltd.
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/rtc.h>
#include <linux/of.h>
#include <soc/bcm2835/raspberrypi-firmware.h>

#define	RTC_TIME		0
#define	RTC_ALARM		1
#define	RTC_ALARM_PENDING	2
#define	RTC_ALARM_ENABLE	3
#define	RTC_BBAT_CHG_VOLTS	4
#define	RTC_BBAT_CHG_VOLTS_MIN	5
#define	RTC_BBAT_CHG_VOLTS_MAX	6
#define	RTC_BBAT_VOLTS		7

struct rpi_rtc_data {
	struct rtc_device *rtc;
	struct rpi_firmware *fw;
	u32 bbat_vchg_millivolts;
	u32 bbat_vchg_min_millivolts;
	u32 bbat_vchg_max_millivolts;
};

struct rpi_rtc_reg {
	__le32 reg;
	__le32 val;
} __packed;

static int rpi_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
	struct rpi_rtc_data *rtc = dev_get_drvdata(dev);
	struct rpi_rtc_reg data = {
		.reg = cpu_to_le32(RTC_TIME)
	};
	int err;

	err = rpi_firmware_property(rtc->fw, RPI_FIRMWARE_GET_RTC_REG,
				    &data, sizeof(data));
	rtc_time64_to_tm(le32_to_cpu(data.val), tm);
	return err;
}

static int rpi_rtc_set_time(struct device *dev, struct rtc_time *tm)
{
	struct rpi_rtc_data *rtc = dev_get_drvdata(dev);
	struct rpi_rtc_reg data = {
		.reg = cpu_to_le32(RTC_TIME),
		.val = cpu_to_le32(rtc_tm_to_time64(tm))
	};

	return rpi_firmware_property(rtc->fw, RPI_FIRMWARE_SET_RTC_REG,
				     &data, sizeof(data));
}

static int rpi_rtc_alarm_irq_is_enabled(struct device *dev, unsigned char *enabled)
{
	struct rpi_rtc_data *rtc = dev_get_drvdata(dev);
	struct rpi_rtc_reg data = {
		.reg = cpu_to_le32(RTC_ALARM_ENABLE)
	};
	int err;

	err = rpi_firmware_property(rtc->fw, RPI_FIRMWARE_GET_RTC_REG,
				    &data, sizeof(data));
	*enabled = le32_to_cpu(data.val) & 0x1;
	return err;
}

static int rpi_rtc_alarm_irq_enable(struct device *dev, unsigned int enabled)
{
	struct rpi_rtc_data *rtc = dev_get_drvdata(dev);
	struct rpi_rtc_reg data = {
		.reg = cpu_to_le32(RTC_ALARM_ENABLE),
		.val = cpu_to_le32(enabled)
	};

	return rpi_firmware_property(rtc->fw, RPI_FIRMWARE_SET_RTC_REG,
				     &data, sizeof(data));
}

static int rpi_rtc_alarm_clear_pending(struct device *dev)
{
	struct rpi_rtc_data *rtc = dev_get_drvdata(dev);
	struct rpi_rtc_reg data = {
		.reg = cpu_to_le32(RTC_ALARM_PENDING),
		.val = cpu_to_le32(1)
	};

	return rpi_firmware_property(rtc->fw, RPI_FIRMWARE_SET_RTC_REG,
				     &data, sizeof(data));
}

static int rpi_rtc_read_alarm(struct device *dev, struct rtc_wkalrm *alarm)
{
	struct rpi_rtc_data *rtc = dev_get_drvdata(dev);
	struct rpi_rtc_reg data = {
		.reg = cpu_to_le32(RTC_ALARM)
	};
	int err;

	err = rpi_rtc_alarm_irq_is_enabled(dev, &alarm->enabled);
	if (!err)
		err = rpi_firmware_property(rtc->fw, RPI_FIRMWARE_GET_RTC_REG,
					    &data, sizeof(data));
	rtc_time64_to_tm(le32_to_cpu(data.val), &alarm->time);

	return err;
}

static int rpi_rtc_set_alarm(struct device *dev, struct rtc_wkalrm *alarm)
{
	struct rpi_rtc_data *rtc = dev_get_drvdata(dev);
	struct rpi_rtc_reg data = {
		.reg = cpu_to_le32(RTC_ALARM),
		.val = cpu_to_le32(rtc_tm_to_time64(&alarm->time))
	};
	int err;

	err = rpi_rtc_alarm_clear_pending(dev);

	if (!err)
		err = rpi_firmware_property(rtc->fw, RPI_FIRMWARE_SET_RTC_REG,
					    &data, sizeof(data));

	if (!err)
		err = rpi_rtc_alarm_irq_enable(dev, alarm->enabled);

	return err;
}

static const struct rtc_class_ops rpi_rtc_ops = {
	.read_time = rpi_rtc_read_time,
	.set_time = rpi_rtc_set_time,
	.read_alarm = rpi_rtc_read_alarm,
	.set_alarm = rpi_rtc_set_alarm,
	.alarm_irq_enable = rpi_rtc_alarm_irq_enable,
};

static void rpi_rtc_set_limits(struct device *dev)
{
	struct rpi_rtc_data *rtc = dev_get_drvdata(dev);
	struct rpi_rtc_reg data = {
		.reg = cpu_to_le32(RTC_BBAT_CHG_VOLTS_MIN)
	};

	int err = rpi_firmware_property(rtc->fw, RPI_FIRMWARE_GET_RTC_REG,
					&data, sizeof(data));
	if (!err)
		rtc->bbat_vchg_min_millivolts = le32_to_cpu(data.val) / 1000U;
	else
		dev_warn(dev, "failed to get minimum trickle charge voltage\n");

	data.reg = cpu_to_le32(RTC_BBAT_CHG_VOLTS_MAX);
	data.val = cpu_to_le32(0);
	err = rpi_firmware_property(rtc->fw, RPI_FIRMWARE_GET_RTC_REG,
				    &data, sizeof(data));
	if (!err)
		rtc->bbat_vchg_max_millivolts = le32_to_cpu(data.val) / 1000U;
	else
		dev_warn(dev, "failed to get maximum trickle charge voltage\n");
}

static void rpi_rtc_set_charge_voltage(struct device *dev)
{
	struct rpi_rtc_data *rtc = dev_get_drvdata(dev);
	struct rpi_rtc_reg data = {
		.reg = cpu_to_le32(RTC_BBAT_CHG_VOLTS),
		.val = cpu_to_le32(rtc->bbat_vchg_millivolts * 1000U)
	};
	int err;

	if (rtc->bbat_vchg_millivolts != 0 &&
	    (rtc->bbat_vchg_millivolts < rtc->bbat_vchg_min_millivolts ||
	    rtc->bbat_vchg_millivolts > rtc->bbat_vchg_max_millivolts)) {
		dev_warn(dev, "trickle charge voltage %umV is outside of the supported range (%umV - %umV)\n",
			 rtc->bbat_vchg_millivolts,
			 rtc->bbat_vchg_min_millivolts,
			 rtc->bbat_vchg_max_millivolts);
		return;
	}

	err = rpi_firmware_property(rtc->fw, RPI_FIRMWARE_SET_RTC_REG,
				    &data, sizeof(data));

	if (err)
		dev_err(dev, "failed to set trickle charge voltage to %umV: %d\n",
			rtc->bbat_vchg_millivolts, err);
	else if (rtc->bbat_vchg_millivolts)
		dev_info(dev, "trickle charging enabled at %umV\n",
			 rtc->bbat_vchg_millivolts);
}

static int rpi_rtc_probe(struct platform_device *pdev)
{
	struct rpi_rtc_data *rtc;
	struct device *dev = &pdev->dev;
	struct rpi_firmware *firmware;
	int ret;

	// Get the firmware device from the parent device
	firmware = devm_rpi_firmware_get(dev, dev->parent->of_node);

	if (!firmware)
		return -EPROBE_DEFER;

	rtc = devm_kzalloc(dev, sizeof(*rtc), GFP_KERNEL);
	if (!rtc)
		return -ENOMEM;

	rtc->fw = firmware;

	ret = devm_device_init_wakeup(dev);
	if (ret)
		return ret;

	platform_set_drvdata(pdev, rtc);

	rtc->rtc = devm_rtc_allocate_device(dev);
	if (IS_ERR(rtc->rtc))
		return PTR_ERR(rtc->rtc);

	rtc->rtc->range_max = U32_MAX;           /* 2106-02-07 */

	set_bit(RTC_FEATURE_ALARM_WAKEUP_ONLY, rtc->rtc->features);
	clear_bit(RTC_FEATURE_UPDATE_INTERRUPT, rtc->rtc->features);

	rtc->rtc->ops = &rpi_rtc_ops;

	rpi_rtc_alarm_clear_pending(dev);

	/*
	 * The trickle-voltage property lives on the parent firmware node
	 * because we deliberately do not create a DT child node just to
	 * instantiate this driver (see DT maintainer guidance).
	 * The firmware driver registers us as a platform device at runtime.
	 */
	rtc->bbat_vchg_millivolts = 0;
	of_property_read_u32(dev->parent->of_node, "trickle-voltage-millivolt",
			     &rtc->bbat_vchg_millivolts);

	rpi_rtc_set_limits(dev);
	rpi_rtc_set_charge_voltage(dev);

	return devm_rtc_register_device(rtc->rtc);
}

static const struct platform_device_id raspberrypi_rtc_id[] = {
	{ "raspberrypi-rtc" },
	{ }
};
MODULE_DEVICE_TABLE(platform, raspberrypi_rtc_id);

static struct platform_driver rpi_rtc_driver = {
	.driver = {
		.name = "raspberrypi-rtc",
	},
	.id_table = raspberrypi_rtc_id,
	.probe = rpi_rtc_probe
};

module_platform_driver(rpi_rtc_driver);

MODULE_AUTHOR("Jonathan Bell <jonathan@raspberrypi.com>");
MODULE_AUTHOR("Sander Speetjens <sander.speetjens@gmail.com>");
MODULE_DESCRIPTION("Raspberry Pi RTC driver");
MODULE_LICENSE("Dual BSD/GPL");
