// SPDX-License-Identifier: GPL-2.0+
/*
 * hwmon driver for Aquacomputer D5 Next watercooling pump
 *
 * The D5 Next sends HID reports (with ID 0x01) every second to report sensor values
 * (coolant temperature, pump and fan speed, voltage, current and power). It responds to
 * Get_Report requests, but returns a dummy value of no use.
 *
 * Copyright 2021 Aleksa Savic <savicaleksa83@gmail.com>
 */

#include <asm/unaligned.h>
#include <linux/debugfs.h>
#include <linux/hid.h>
#include <linux/hwmon.h>
#include <linux/jiffies.h>
#include <linux/module.h>
#include <linux/seq_file.h>

#define DRIVER_NAME			"aquacomputer-d5next"

#define D5NEXT_STATUS_REPORT_ID	0x01
#define D5NEXT_STATUS_UPDATE_INTERVAL	(2 * HZ) /* In seconds */

/* Register offsets for the D5 Next pump */

#define D5NEXT_SERIAL_FIRST_PART	3
#define D5NEXT_SERIAL_SECOND_PART	5
#define D5NEXT_FIRMWARE_VERSION	13
#define D5NEXT_POWER_CYCLES		24

#define D5NEXT_COOLANT_TEMP		87

#define D5NEXT_PUMP_SPEED		116
#define D5NEXT_FAN_SPEED		103

#define D5NEXT_PUMP_POWER		114
#define D5NEXT_FAN_POWER		101

#define D5NEXT_PUMP_VOLTAGE		110
#define D5NEXT_FAN_VOLTAGE		97
#define D5NEXT_5V_VOLTAGE		57

#define D5NEXT_PUMP_CURRENT		112
#define D5NEXT_FAN_CURRENT		99

/* Labels for provided values */

#define L_COOLANT_TEMP			"Coolant temp"

#define L_PUMP_SPEED			"Pump speed"
#define L_FAN_SPEED			"Fan speed"

#define L_PUMP_POWER			"Pump power"
#define L_FAN_POWER			"Fan power"

#define L_PUMP_VOLTAGE			"Pump voltage"
#define L_FAN_VOLTAGE			"Fan voltage"
#define L_5V_VOLTAGE			"+5V voltage"

#define L_PUMP_CURRENT			"Pump current"
#define L_FAN_CURRENT			"Fan current"

static const char *const label_speeds[] = {
	L_PUMP_SPEED,
	L_FAN_SPEED,
};

static const char *const label_power[] = {
	L_PUMP_POWER,
	L_FAN_POWER,
};

static const char *const label_voltages[] = {
	L_PUMP_VOLTAGE,
	L_FAN_VOLTAGE,
	L_5V_VOLTAGE,
};

static const char *const label_current[] = {
	L_PUMP_CURRENT,
	L_FAN_CURRENT,
};

struct d5next_data {
	struct hid_device *hdev;
	struct device *hwmon_dev;
	struct dentry *debugfs;
	s32 temp_input;
	u16 speed_input[2];
	u32 power_input[2];
	u16 voltage_input[3];
	u16 current_input[2];
	u32 serial_number[2];
	u16 firmware_version;
	u32 power_cycles; /* How many times the device was powered on */
	unsigned long updated;
};

static umode_t d5next_is_visible(const void *data, enum hwmon_sensor_types type, u32 attr,
				 int channel)
{
	return 0444;
}

static int d5next_read(struct device *dev, enum hwmon_sensor_types type, u32 attr, int channel,
		       long *val)
{
	struct d5next_data *priv = dev_get_drvdata(dev);

	if (time_after(jiffies, priv->updated + D5NEXT_STATUS_UPDATE_INTERVAL))
		return -ENODATA;

	switch (type) {
	case hwmon_temp:
		*val = priv->temp_input;
		break;
	case hwmon_fan:
		*val = priv->speed_input[channel];
		break;
	case hwmon_power:
		*val = priv->power_input[channel];
		break;
	case hwmon_in:
		*val = priv->voltage_input[channel];
		break;
	case hwmon_curr:
		*val = priv->current_input[channel];
		break;
	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

static int d5next_read_string(struct device *dev, enum hwmon_sensor_types type, u32 attr,
			      int channel, const char **str)
{
	switch (type) {
	case hwmon_temp:
		*str = L_COOLANT_TEMP;
		break;
	case hwmon_fan:
		*str = label_speeds[channel];
		break;
	case hwmon_power:
		*str = label_power[channel];
		break;
	case hwmon_in:
		*str = label_voltages[channel];
		break;
	case hwmon_curr:
		*str = label_current[channel];
		break;
	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

static const struct hwmon_ops d5next_hwmon_ops = {
	.is_visible = d5next_is_visible,
	.read = d5next_read,
	.read_string = d5next_read_string,
};

static const struct hwmon_channel_info *d5next_info[] = {
	HWMON_CHANNEL_INFO(temp, HWMON_T_INPUT | HWMON_T_LABEL),
	HWMON_CHANNEL_INFO(fan, HWMON_F_INPUT | HWMON_F_LABEL, HWMON_F_INPUT | HWMON_F_LABEL),
	HWMON_CHANNEL_INFO(power, HWMON_P_INPUT | HWMON_P_LABEL, HWMON_P_INPUT | HWMON_P_LABEL),
	HWMON_CHANNEL_INFO(in, HWMON_I_INPUT | HWMON_I_LABEL, HWMON_I_INPUT | HWMON_I_LABEL,
			   HWMON_I_INPUT | HWMON_I_LABEL),
	HWMON_CHANNEL_INFO(curr, HWMON_C_INPUT | HWMON_C_LABEL, HWMON_C_INPUT | HWMON_C_LABEL),
	NULL
};

static const struct hwmon_chip_info d5next_chip_info = {
	.ops = &d5next_hwmon_ops,
	.info = d5next_info,
};

static int d5next_raw_event(struct hid_device *hdev, struct hid_report *report, u8 *data, int size)
{
	struct d5next_data *priv;

	if (report->id != D5NEXT_STATUS_REPORT_ID)
		return 0;

	priv = hid_get_drvdata(hdev);

	/* Info provided with every report */

	priv->serial_number[0] = get_unaligned_be16(data + D5NEXT_SERIAL_FIRST_PART);
	priv->serial_number[1] = get_unaligned_be16(data + D5NEXT_SERIAL_SECOND_PART);

	priv->firmware_version = get_unaligned_be16(data + D5NEXT_FIRMWARE_VERSION);
	priv->power_cycles = get_unaligned_be32(data + D5NEXT_POWER_CYCLES);

	/* Sensor readings */

	priv->temp_input = get_unaligned_be16(data + D5NEXT_COOLANT_TEMP) * 10;

	priv->speed_input[0] = get_unaligned_be16(data + D5NEXT_PUMP_SPEED);
	priv->speed_input[1] = get_unaligned_be16(data + D5NEXT_FAN_SPEED);

	priv->power_input[0] = get_unaligned_be16(data + D5NEXT_PUMP_POWER) * 10000;
	priv->power_input[1] = get_unaligned_be16(data + D5NEXT_FAN_POWER) * 10000;

	priv->voltage_input[0] = get_unaligned_be16(data + D5NEXT_PUMP_VOLTAGE) * 10;
	priv->voltage_input[1] = get_unaligned_be16(data + D5NEXT_FAN_VOLTAGE) * 10;
	priv->voltage_input[2] = get_unaligned_be16(data + D5NEXT_5V_VOLTAGE) * 10;

	priv->current_input[0] = get_unaligned_be16(data + D5NEXT_PUMP_CURRENT);
	priv->current_input[1] = get_unaligned_be16(data + D5NEXT_FAN_CURRENT);

	priv->updated = jiffies;

	return 0;
}

#ifdef CONFIG_DEBUG_FS

static int serial_number_show(struct seq_file *seqf, void *unused)
{
	struct d5next_data *priv = seqf->private;

	seq_printf(seqf, "%05u-%05u\n", priv->serial_number[0], priv->serial_number[1]);

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(serial_number);

static int firmware_version_show(struct seq_file *seqf, void *unused)
{
	struct d5next_data *priv = seqf->private;

	seq_printf(seqf, "%u\n", priv->firmware_version);

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(firmware_version);

static int power_cycles_show(struct seq_file *seqf, void *unused)
{
	struct d5next_data *priv = seqf->private;

	seq_printf(seqf, "%u\n", priv->power_cycles);

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(power_cycles);

static void d5next_debugfs_init(struct d5next_data *priv)
{
	char name[32];

	scnprintf(name, sizeof(name), "%s-%s", DRIVER_NAME, dev_name(&priv->hdev->dev));

	priv->debugfs = debugfs_create_dir(name, NULL);
	debugfs_create_file("serial_number", 0444, priv->debugfs, priv, &serial_number_fops);
	debugfs_create_file("firmware_version", 0444, priv->debugfs, priv, &firmware_version_fops);
	debugfs_create_file("power_cycles", 0444, priv->debugfs, priv, &power_cycles_fops);
}

#else

static void d5next_debugfs_init(struct d5next_data *priv)
{
}

#endif

static int d5next_probe(struct hid_device *hdev, const struct hid_device_id *id)
{
	struct d5next_data *priv;
	int ret;

	priv = devm_kzalloc(&hdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->hdev = hdev;
	hid_set_drvdata(hdev, priv);

	priv->updated = jiffies - D5NEXT_STATUS_UPDATE_INTERVAL;

	ret = hid_parse(hdev);
	if (ret)
		return ret;

	ret = hid_hw_start(hdev, HID_CONNECT_HIDRAW);
	if (ret)
		return ret;

	ret = hid_hw_open(hdev);
	if (ret)
		goto fail_and_stop;

	priv->hwmon_dev = hwmon_device_register_with_info(&hdev->dev, "d5next", priv,
							  &d5next_chip_info, NULL);

	if (IS_ERR(priv->hwmon_dev)) {
		ret = PTR_ERR(priv->hwmon_dev);
		goto fail_and_close;
	}

	d5next_debugfs_init(priv);

	return 0;

fail_and_close:
	hid_hw_close(hdev);
fail_and_stop:
	hid_hw_stop(hdev);
	return ret;
}

static void d5next_remove(struct hid_device *hdev)
{
	struct d5next_data *priv = hid_get_drvdata(hdev);

	debugfs_remove_recursive(priv->debugfs);
	hwmon_device_unregister(priv->hwmon_dev);

	hid_hw_close(hdev);
	hid_hw_stop(hdev);
}

static const struct hid_device_id d5next_table[] = {
	{ HID_USB_DEVICE(0x0c70, 0xf00e) }, /* Aquacomputer D5 Next */
	{},
};

MODULE_DEVICE_TABLE(hid, d5next_table);

static struct hid_driver d5next_driver = {
	.name = DRIVER_NAME,
	.id_table = d5next_table,
	.probe = d5next_probe,
	.remove = d5next_remove,
	.raw_event = d5next_raw_event,
};

static int __init d5next_init(void)
{
	return hid_register_driver(&d5next_driver);
}

static void __exit d5next_exit(void)
{
	hid_unregister_driver(&d5next_driver);
}

/* Request to initialize after the HID bus to ensure it's not being loaded before */

late_initcall(d5next_init);
module_exit(d5next_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Aleksa Savic <savicaleksa83@gmail.com>");
MODULE_DESCRIPTION("Hwmon driver for Aquacomputer D5 Next pump");
