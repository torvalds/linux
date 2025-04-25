// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2025 KEBA Industrial Automation GmbH
 *
 * Driver for KEBA fan controller FPGA IP core
 *
 */

#include <linux/hwmon.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/auxiliary_bus.h>
#include <linux/misc/keba.h>

#define KFAN "kfan"

#define KFAN_CONTROL_REG	0x04

#define KFAN_STATUS_REG		0x08
#define   KFAN_STATUS_PRESENT	0x01
#define   KFAN_STATUS_REGULABLE	0x02
#define   KFAN_STATUS_TACHO	0x04
#define   KFAN_STATUS_BLOCKED	0x08

#define KFAN_TACHO_REG		0x0c

#define KFAN_DEFAULT_DIV	2

struct kfan {
	void __iomem *base;
	bool tacho;
	bool regulable;

	/* hwmon API configuration */
	u32 fan_channel_config[2];
	struct hwmon_channel_info fan_info;
	u32 pwm_channel_config[2];
	struct hwmon_channel_info pwm_info;
	const struct hwmon_channel_info *info[3];
	struct hwmon_chip_info chip;
};

static bool kfan_get_fault(struct kfan *kfan)
{
	u8 status = ioread8(kfan->base + KFAN_STATUS_REG);

	if (!(status & KFAN_STATUS_PRESENT))
		return true;

	if (!kfan->tacho && (status & KFAN_STATUS_BLOCKED))
		return true;

	return false;
}

static unsigned int kfan_count_to_rpm(u16 count)
{
	if (count == 0 || count == 0xffff)
		return 0;

	return 5000000UL / (KFAN_DEFAULT_DIV * count);
}

static unsigned int kfan_get_rpm(struct kfan *kfan)
{
	unsigned int rpm;
	u16 count;

	count = ioread16(kfan->base + KFAN_TACHO_REG);
	rpm = kfan_count_to_rpm(count);

	return rpm;
}

static unsigned int kfan_get_pwm(struct kfan *kfan)
{
	return ioread8(kfan->base + KFAN_CONTROL_REG);
}

static int kfan_set_pwm(struct kfan *kfan, long val)
{
	if (val < 0 || val > 0xff)
		return -EINVAL;

	/* if none-regulable, then only 0 or 0xff can be written */
	if (!kfan->regulable && val > 0)
		val = 0xff;

	iowrite8(val, kfan->base + KFAN_CONTROL_REG);

	return 0;
}

static int kfan_write(struct device *dev, enum hwmon_sensor_types type,
		      u32 attr, int channel, long val)
{
	struct kfan *kfan = dev_get_drvdata(dev);

	switch (type) {
	case hwmon_pwm:
		switch (attr) {
		case hwmon_pwm_input:
			return kfan_set_pwm(kfan, val);
		default:
			break;
		}
		break;
	default:
		break;
	}

	return -EOPNOTSUPP;
}

static int kfan_read(struct device *dev, enum hwmon_sensor_types type,
		     u32 attr, int channel, long *val)
{
	struct kfan *kfan = dev_get_drvdata(dev);

	switch (type) {
	case hwmon_fan:
		switch (attr) {
		case hwmon_fan_fault:
			*val = kfan_get_fault(kfan);
			return 0;
		case hwmon_fan_input:
			*val = kfan_get_rpm(kfan);
			return 0;
		default:
			break;
		}
		break;
	case hwmon_pwm:
		switch (attr) {
		case hwmon_pwm_input:
			*val = kfan_get_pwm(kfan);
			return 0;
		default:
			break;
		}
		break;
	default:
		break;
	}

	return -EOPNOTSUPP;
}

static umode_t kfan_is_visible(const void *data, enum hwmon_sensor_types type,
			       u32 attr, int channel)
{
	switch (type) {
	case hwmon_fan:
		switch (attr) {
		case hwmon_fan_input:
			return 0444;
		case hwmon_fan_fault:
			return 0444;
		default:
			break;
		}
		break;
	case hwmon_pwm:
		switch (attr) {
		case hwmon_pwm_input:
			return 0644;
		default:
			break;
		}
		break;
	default:
		break;
	}

	return 0;
}

static const struct hwmon_ops kfan_hwmon_ops = {
	.is_visible = kfan_is_visible,
	.read = kfan_read,
	.write = kfan_write,
};

static int kfan_probe(struct auxiliary_device *auxdev,
		      const struct auxiliary_device_id *id)
{
	struct keba_fan_auxdev *kfan_auxdev =
		container_of(auxdev, struct keba_fan_auxdev, auxdev);
	struct device *dev = &auxdev->dev;
	struct device *hwmon_dev;
	struct kfan *kfan;
	u8 status;

	kfan = devm_kzalloc(dev, sizeof(*kfan), GFP_KERNEL);
	if (!kfan)
		return -ENOMEM;

	kfan->base = devm_ioremap_resource(dev, &kfan_auxdev->io);
	if (IS_ERR(kfan->base))
		return PTR_ERR(kfan->base);

	status = ioread8(kfan->base + KFAN_STATUS_REG);
	if (status & KFAN_STATUS_REGULABLE)
		kfan->regulable = true;
	if (status & KFAN_STATUS_TACHO)
		kfan->tacho = true;

	/* fan */
	kfan->fan_channel_config[0] = HWMON_F_FAULT;
	if (kfan->tacho)
		kfan->fan_channel_config[0] |= HWMON_F_INPUT;
	kfan->fan_info.type = hwmon_fan;
	kfan->fan_info.config = kfan->fan_channel_config;
	kfan->info[0] = &kfan->fan_info;

	/* PWM */
	kfan->pwm_channel_config[0] = HWMON_PWM_INPUT;
	kfan->pwm_info.type = hwmon_pwm;
	kfan->pwm_info.config = kfan->pwm_channel_config;
	kfan->info[1] = &kfan->pwm_info;

	kfan->chip.ops = &kfan_hwmon_ops;
	kfan->chip.info = kfan->info;
	hwmon_dev = devm_hwmon_device_register_with_info(dev, KFAN, kfan,
							 &kfan->chip, NULL);
	return PTR_ERR_OR_ZERO(hwmon_dev);
}

static const struct auxiliary_device_id kfan_devtype_aux[] = {
	{ .name = "keba.fan" },
	{}
};
MODULE_DEVICE_TABLE(auxiliary, kfan_devtype_aux);

static struct auxiliary_driver kfan_driver_aux = {
	.name = KFAN,
	.id_table = kfan_devtype_aux,
	.probe = kfan_probe,
};
module_auxiliary_driver(kfan_driver_aux);

MODULE_AUTHOR("Petar Bojanic <boja@keba.com>");
MODULE_AUTHOR("Gerhard Engleder <eg@keba.com>");
MODULE_DESCRIPTION("KEBA fan controller driver");
MODULE_LICENSE("GPL");
