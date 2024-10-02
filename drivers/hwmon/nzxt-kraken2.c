// SPDX-License-Identifier: GPL-2.0+
/*
 * nzxt-kraken2.c - hwmon driver for NZXT Kraken X42/X52/X62/X72 coolers
 *
 * The device asynchronously sends HID reports (with id 0x04) twice a second to
 * communicate current fan speed, pump speed and coolant temperature.  The
 * device does not respond to Get_Report requests for this status report.
 *
 * Copyright 2019-2021  Jonas Malaco <jonas@protocubo.io>
 */

#include <linux/unaligned.h>
#include <linux/hid.h>
#include <linux/hwmon.h>
#include <linux/jiffies.h>
#include <linux/module.h>

#define STATUS_REPORT_ID	0x04
#define STATUS_VALIDITY		2 /* seconds; equivalent to 4 missed updates */

static const char *const kraken2_temp_label[] = {
	"Coolant",
};

static const char *const kraken2_fan_label[] = {
	"Fan",
	"Pump",
};

struct kraken2_priv_data {
	struct hid_device *hid_dev;
	struct device *hwmon_dev;
	s32 temp_input[1];
	u16 fan_input[2];
	unsigned long updated; /* jiffies */
};

static umode_t kraken2_is_visible(const void *data,
				  enum hwmon_sensor_types type,
				  u32 attr, int channel)
{
	return 0444;
}

static int kraken2_read(struct device *dev, enum hwmon_sensor_types type,
			u32 attr, int channel, long *val)
{
	struct kraken2_priv_data *priv = dev_get_drvdata(dev);

	if (time_after(jiffies, priv->updated + STATUS_VALIDITY * HZ))
		return -ENODATA;

	switch (type) {
	case hwmon_temp:
		*val = priv->temp_input[channel];
		break;
	case hwmon_fan:
		*val = priv->fan_input[channel];
		break;
	default:
		return -EOPNOTSUPP; /* unreachable */
	}

	return 0;
}

static int kraken2_read_string(struct device *dev, enum hwmon_sensor_types type,
			       u32 attr, int channel, const char **str)
{
	switch (type) {
	case hwmon_temp:
		*str = kraken2_temp_label[channel];
		break;
	case hwmon_fan:
		*str = kraken2_fan_label[channel];
		break;
	default:
		return -EOPNOTSUPP; /* unreachable */
	}
	return 0;
}

static const struct hwmon_ops kraken2_hwmon_ops = {
	.is_visible = kraken2_is_visible,
	.read = kraken2_read,
	.read_string = kraken2_read_string,
};

static const struct hwmon_channel_info * const kraken2_info[] = {
	HWMON_CHANNEL_INFO(temp,
			   HWMON_T_INPUT | HWMON_T_LABEL),
	HWMON_CHANNEL_INFO(fan,
			   HWMON_F_INPUT | HWMON_F_LABEL,
			   HWMON_F_INPUT | HWMON_F_LABEL),
	NULL
};

static const struct hwmon_chip_info kraken2_chip_info = {
	.ops = &kraken2_hwmon_ops,
	.info = kraken2_info,
};

static int kraken2_raw_event(struct hid_device *hdev,
			     struct hid_report *report, u8 *data, int size)
{
	struct kraken2_priv_data *priv;

	if (size < 7 || report->id != STATUS_REPORT_ID)
		return 0;

	priv = hid_get_drvdata(hdev);

	/*
	 * The fractional byte of the coolant temperature has been observed to
	 * be in the interval [1,9], but some of these steps are also
	 * consistently skipped for certain integer parts.
	 *
	 * For the lack of a better idea, assume that the resolution is 0.1°C,
	 * and that the missing steps are artifacts of how the firmware
	 * processes the raw sensor data.
	 */
	priv->temp_input[0] = data[1] * 1000 + data[2] * 100;

	priv->fan_input[0] = get_unaligned_be16(data + 3);
	priv->fan_input[1] = get_unaligned_be16(data + 5);

	priv->updated = jiffies;

	return 0;
}

static int kraken2_probe(struct hid_device *hdev,
			 const struct hid_device_id *id)
{
	struct kraken2_priv_data *priv;
	int ret;

	priv = devm_kzalloc(&hdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->hid_dev = hdev;
	hid_set_drvdata(hdev, priv);

	/*
	 * Initialize ->updated to STATUS_VALIDITY seconds in the past, making
	 * the initial empty data invalid for kraken2_read without the need for
	 * a special case there.
	 */
	priv->updated = jiffies - STATUS_VALIDITY * HZ;

	ret = hid_parse(hdev);
	if (ret) {
		hid_err(hdev, "hid parse failed with %d\n", ret);
		return ret;
	}

	/*
	 * Enable hidraw so existing user-space tools can continue to work.
	 */
	ret = hid_hw_start(hdev, HID_CONNECT_HIDRAW);
	if (ret) {
		hid_err(hdev, "hid hw start failed with %d\n", ret);
		return ret;
	}

	ret = hid_hw_open(hdev);
	if (ret) {
		hid_err(hdev, "hid hw open failed with %d\n", ret);
		goto fail_and_stop;
	}

	priv->hwmon_dev = hwmon_device_register_with_info(&hdev->dev, "kraken2",
							  priv, &kraken2_chip_info,
							  NULL);
	if (IS_ERR(priv->hwmon_dev)) {
		ret = PTR_ERR(priv->hwmon_dev);
		hid_err(hdev, "hwmon registration failed with %d\n", ret);
		goto fail_and_close;
	}

	return 0;

fail_and_close:
	hid_hw_close(hdev);
fail_and_stop:
	hid_hw_stop(hdev);
	return ret;
}

static void kraken2_remove(struct hid_device *hdev)
{
	struct kraken2_priv_data *priv = hid_get_drvdata(hdev);

	hwmon_device_unregister(priv->hwmon_dev);

	hid_hw_close(hdev);
	hid_hw_stop(hdev);
}

static const struct hid_device_id kraken2_table[] = {
	{ HID_USB_DEVICE(0x1e71, 0x170e) }, /* NZXT Kraken X42/X52/X62/X72 */
	{ }
};

MODULE_DEVICE_TABLE(hid, kraken2_table);

static struct hid_driver kraken2_driver = {
	.name = "nzxt-kraken2",
	.id_table = kraken2_table,
	.probe = kraken2_probe,
	.remove = kraken2_remove,
	.raw_event = kraken2_raw_event,
};

static int __init kraken2_init(void)
{
	return hid_register_driver(&kraken2_driver);
}

static void __exit kraken2_exit(void)
{
	hid_unregister_driver(&kraken2_driver);
}

/*
 * When compiled into the kernel, initialize after the hid bus.
 */
late_initcall(kraken2_init);
module_exit(kraken2_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jonas Malaco <jonas@protocubo.io>");
MODULE_DESCRIPTION("Hwmon driver for NZXT Kraken X42/X52/X62/X72 coolers");
