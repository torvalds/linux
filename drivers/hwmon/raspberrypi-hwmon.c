// SPDX-License-Identifier: GPL-2.0+
/*
 * Raspberry Pi voltage sensor driver
 *
 * Based on firmware/raspberrypi.c by Noralf Trønnes
 *
 * Copyright (C) 2018 Stefan Wahren <stefan.wahren@i2se.com>
 * Copyright (C) 2026 Shubham Chakraborty <chakrabortyshubham66@gmail.com>
 */
#include <linux/device.h>
#include <linux/err.h>
#include <linux/hwmon.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <soc/bcm2835/raspberrypi-firmware.h>

#define UNDERVOLTAGE_STICKY_BIT	BIT(16)

struct rpi_hwmon_data {
	struct device *hwmon_dev;
	struct rpi_firmware *fw;
	u32 valid_inputs;
	u32 last_throttled;
	struct delayed_work get_values_poll_work;
};

static const char * const rpi_hwmon_labels[] = {
	"core",
	"sdram_c",
	"sdram_i",
	"sdram_p",
};

static void rpi_firmware_get_throttled(struct rpi_hwmon_data *data)
{
	u32 new_uv, old_uv, value;
	int ret;

	/* Request firmware to clear sticky bits */
	value = 0xffff;

	ret = rpi_firmware_property(data->fw, RPI_FIRMWARE_GET_THROTTLED,
				    &value, sizeof(value));
	if (ret) {
		dev_err_once(data->hwmon_dev, "Failed to get throttled (%d)\n",
			     ret);
		return;
	}

	new_uv = value & UNDERVOLTAGE_STICKY_BIT;
	old_uv = data->last_throttled & UNDERVOLTAGE_STICKY_BIT;
	data->last_throttled = value;

	if (new_uv == old_uv)
		return;

	if (new_uv)
		dev_crit(data->hwmon_dev, "Undervoltage detected!\n");
	else
		dev_info(data->hwmon_dev, "Voltage normalised\n");

	hwmon_notify_event(data->hwmon_dev, hwmon_in, hwmon_in_lcrit_alarm, 0);
}

static int rpi_firmware_get_voltage(struct rpi_hwmon_data *data, u32 id,
				    long *val)
{
	struct rpi_firmware_get_voltage_request packet =
		RPI_FIRMWARE_GET_VOLTAGE_REQUEST(id);
	int ret;

	ret = rpi_firmware_property(data->fw, RPI_FIRMWARE_GET_VOLTAGE,
				    &packet, sizeof(packet));
	if (ret)
		return ret;

	*val = le32_to_cpu(packet.value) / 1000;
	return 0;
}

static void get_values_poll(struct work_struct *work)
{
	struct rpi_hwmon_data *data;

	data = container_of(work, struct rpi_hwmon_data,
			    get_values_poll_work.work);

	rpi_firmware_get_throttled(data);

	/*
	 * We can't run faster than the sticky shift (100ms) since we get
	 * flipping in the sticky bits that are cleared.
	 */
	schedule_delayed_work(&data->get_values_poll_work, 2 * HZ);
}

static void rpi_hwmon_cancel_poll_work(void *res)
{
	struct rpi_hwmon_data *data = res;

	disable_delayed_work_sync(&data->get_values_poll_work);
}

static int rpi_read(struct device *dev, enum hwmon_sensor_types type,
		    u32 attr, int channel, long *val)
{
	struct rpi_hwmon_data *data = dev_get_drvdata(dev);

	if (type == hwmon_in) {
		switch (attr) {
		case hwmon_in_input:
			switch (channel) {
			case 0:
				return rpi_firmware_get_voltage(data,
						RPI_FIRMWARE_VOLT_ID_CORE,
						val);
			case 1:
				return rpi_firmware_get_voltage(data,
						RPI_FIRMWARE_VOLT_ID_SDRAM_C,
						val);
			case 2:
				return rpi_firmware_get_voltage(data,
						RPI_FIRMWARE_VOLT_ID_SDRAM_I,
						val);
			case 3:
				return rpi_firmware_get_voltage(data,
						RPI_FIRMWARE_VOLT_ID_SDRAM_P,
						val);
			default:
				return -EOPNOTSUPP;
			}
		case hwmon_in_lcrit_alarm:
			if (channel == 0) {
				*val = !!(data->last_throttled & UNDERVOLTAGE_STICKY_BIT);
				return 0;
			}
			return -EOPNOTSUPP;
		default:
			return -EOPNOTSUPP;
		}
	}

	return -EOPNOTSUPP;
}

static int rpi_read_string(struct device *dev, enum hwmon_sensor_types type,
			   u32 attr, int channel, const char **str)
{
	if (type == hwmon_in && attr == hwmon_in_label) {
		if (channel >= ARRAY_SIZE(rpi_hwmon_labels))
			return -EOPNOTSUPP;

		*str = rpi_hwmon_labels[channel];
		return 0;
	}

	return -EOPNOTSUPP;
}

static umode_t rpi_is_visible(const void *_data, enum hwmon_sensor_types type,
			      u32 attr, int channel)
{
	const struct rpi_hwmon_data *data = _data;

	if (type == hwmon_in) {
		switch (attr) {
		case hwmon_in_input:
		case hwmon_in_label:
			if (!(data->valid_inputs & BIT(channel)))
				return 0;
			return 0444;
		case hwmon_in_lcrit_alarm:
			if (channel == 0)
				return 0444;
			return 0;
		default:
			return 0;
		}
	}

	return 0;
}

static const struct hwmon_channel_info * const rpi_info[] = {
	HWMON_CHANNEL_INFO(in,
			   HWMON_I_INPUT | HWMON_I_LABEL | HWMON_I_LCRIT_ALARM,
			   HWMON_I_INPUT | HWMON_I_LABEL,
			   HWMON_I_INPUT | HWMON_I_LABEL,
			   HWMON_I_INPUT | HWMON_I_LABEL),
	NULL
};

static const struct hwmon_ops rpi_hwmon_ops = {
	.is_visible = rpi_is_visible,
	.read = rpi_read,
	.read_string = rpi_read_string,
};

static const struct hwmon_chip_info rpi_chip_info = {
	.ops = &rpi_hwmon_ops,
	.info = rpi_info,
};

static int rpi_hwmon_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct rpi_hwmon_data *data;
	long voltage;
	int ret;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	/* Parent driver assure that firmware is correct */
	data->fw = dev_get_drvdata(dev->parent);

	ret = rpi_firmware_get_voltage(data, RPI_FIRMWARE_VOLT_ID_CORE,
				       &voltage);
	if (!ret)
		data->valid_inputs |= BIT(0);

	ret = rpi_firmware_get_voltage(data, RPI_FIRMWARE_VOLT_ID_SDRAM_C,
				       &voltage);
	if (!ret)
		data->valid_inputs |= BIT(1);

	ret = rpi_firmware_get_voltage(data, RPI_FIRMWARE_VOLT_ID_SDRAM_I,
				       &voltage);
	if (!ret)
		data->valid_inputs |= BIT(2);

	ret = rpi_firmware_get_voltage(data, RPI_FIRMWARE_VOLT_ID_SDRAM_P,
				       &voltage);
	if (!ret)
		data->valid_inputs |= BIT(3);

	data->hwmon_dev = devm_hwmon_device_register_with_info(dev, "rpi_volt",
							       data,
							       &rpi_chip_info,
							       NULL);
	if (IS_ERR(data->hwmon_dev))
		return PTR_ERR(data->hwmon_dev);

	INIT_DELAYED_WORK(&data->get_values_poll_work, get_values_poll);
	ret = devm_add_action_or_reset(dev, rpi_hwmon_cancel_poll_work, data);
	if (ret)
		return ret;
	platform_set_drvdata(pdev, data);

	schedule_delayed_work(&data->get_values_poll_work, 2 * HZ);

	return 0;
}

static int rpi_hwmon_suspend(struct device *dev)
{
	struct rpi_hwmon_data *data = dev_get_drvdata(dev);

	cancel_delayed_work_sync(&data->get_values_poll_work);

	return 0;
}

static int rpi_hwmon_resume(struct device *dev)
{
	struct rpi_hwmon_data *data = dev_get_drvdata(dev);

	get_values_poll(&data->get_values_poll_work.work);

	return 0;
}

static DEFINE_SIMPLE_DEV_PM_OPS(rpi_hwmon_pm_ops, rpi_hwmon_suspend,
				rpi_hwmon_resume);

static struct platform_driver rpi_hwmon_driver = {
	.probe = rpi_hwmon_probe,
	.driver = {
		.name = "raspberrypi-hwmon",
		.pm = pm_ptr(&rpi_hwmon_pm_ops),
	},
};
module_platform_driver(rpi_hwmon_driver);

MODULE_AUTHOR("Stefan Wahren <wahrenst@gmx.net>");
MODULE_AUTHOR("Shubham Chakraborty <chakrabortyshubham66@gmail.com>");
MODULE_DESCRIPTION("Raspberry Pi voltage sensor driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:raspberrypi-hwmon");
