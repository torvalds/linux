// SPDX-License-Identifier: GPL-2.0+
/*
 * hwmon driver for Aquacomputer devices (D5 Next, Farbwerk 360, Octo)
 *
 * Aquacomputer devices send HID reports (with ID 0x01) every second to report
 * sensor values.
 *
 * Copyright 2021 Aleksa Savic <savicaleksa83@gmail.com>
 */

#include <linux/crc16.h>
#include <linux/debugfs.h>
#include <linux/hid.h>
#include <linux/hwmon.h>
#include <linux/jiffies.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/seq_file.h>
#include <asm/unaligned.h>

#define USB_VENDOR_ID_AQUACOMPUTER	0x0c70
#define USB_PRODUCT_ID_D5NEXT		0xf00e
#define USB_PRODUCT_ID_FARBWERK360	0xf010
#define USB_PRODUCT_ID_OCTO		0xf011

enum kinds { d5next, farbwerk360, octo };

static const char *const aqc_device_names[] = {
	[d5next] = "d5next",
	[farbwerk360] = "farbwerk360",
	[octo] = "octo"
};

#define DRIVER_NAME			"aquacomputer_d5next"

#define STATUS_REPORT_ID		0x01
#define STATUS_UPDATE_INTERVAL		(2 * HZ)	/* In seconds */
#define SERIAL_FIRST_PART		3
#define SERIAL_SECOND_PART		5
#define FIRMWARE_VERSION		13

#define CTRL_REPORT_ID			0x03

/* The HID report that the official software always sends
 * after writing values, currently same for all devices
 */
#define SECONDARY_CTRL_REPORT_ID	0x02
#define SECONDARY_CTRL_REPORT_SIZE	0x0B

static u8 secondary_ctrl_report[] = {
	0x02, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x34, 0xC6
};

/* Register offsets for the D5 Next pump */
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

/* Register offsets for the Farbwerk 360 RGB controller */
#define FARBWERK360_NUM_SENSORS		4
#define FARBWERK360_SENSOR_START	0x32
#define FARBWERK360_SENSOR_SIZE		0x02
#define FARBWERK360_SENSOR_DISCONNECTED	0x7FFF

/* Register offsets for the Octo fan controller */
#define OCTO_POWER_CYCLES		0x18
#define OCTO_NUM_FANS			8
#define OCTO_FAN_PERCENT_OFFSET		0x00
#define OCTO_FAN_VOLTAGE_OFFSET		0x02
#define OCTO_FAN_CURRENT_OFFSET		0x04
#define OCTO_FAN_POWER_OFFSET		0x06
#define OCTO_FAN_SPEED_OFFSET		0x08

static u8 octo_sensor_fan_offsets[] = { 0x7D, 0x8A, 0x97, 0xA4, 0xB1, 0xBE, 0xCB, 0xD8 };

#define OCTO_NUM_SENSORS		4
#define OCTO_SENSOR_START		0x3D
#define OCTO_SENSOR_SIZE		0x02
#define OCTO_SENSOR_DISCONNECTED	0x7FFF

#define OCTO_CTRL_REPORT_SIZE			0x65F
#define OCTO_CTRL_REPORT_CHECKSUM_OFFSET	0x65D
#define OCTO_CTRL_REPORT_CHECKSUM_START		0x01
#define OCTO_CTRL_REPORT_CHECKSUM_LENGTH	0x65C

/* Fan speed registers in Octo control report (from 0-100%) */
static u16 octo_ctrl_fan_offsets[] = { 0x5B, 0xB0, 0x105, 0x15A, 0x1AF, 0x204, 0x259, 0x2AE };

/* Labels for D5 Next */
static const char *const label_d5next_temp[] = {
	"Coolant temp"
};

static const char *const label_d5next_speeds[] = {
	"Pump speed",
	"Fan speed"
};

static const char *const label_d5next_power[] = {
	"Pump power",
	"Fan power"
};

static const char *const label_d5next_voltages[] = {
	"Pump voltage",
	"Fan voltage",
	"+5V voltage"
};

static const char *const label_d5next_current[] = {
	"Pump current",
	"Fan current"
};

/* Labels for Farbwerk 360 and Octo temperature sensors */
static const char *const label_temp_sensors[] = {
	"Sensor 1",
	"Sensor 2",
	"Sensor 3",
	"Sensor 4"
};

/* Labels for Octo */
static const char *const label_fan_speed[] = {
	"Fan 1 speed",
	"Fan 2 speed",
	"Fan 3 speed",
	"Fan 4 speed",
	"Fan 5 speed",
	"Fan 6 speed",
	"Fan 7 speed",
	"Fan 8 speed"
};

static const char *const label_fan_power[] = {
	"Fan 1 power",
	"Fan 2 power",
	"Fan 3 power",
	"Fan 4 power",
	"Fan 5 power",
	"Fan 6 power",
	"Fan 7 power",
	"Fan 8 power"
};

static const char *const label_fan_voltage[] = {
	"Fan 1 voltage",
	"Fan 2 voltage",
	"Fan 3 voltage",
	"Fan 4 voltage",
	"Fan 5 voltage",
	"Fan 6 voltage",
	"Fan 7 voltage",
	"Fan 8 voltage"
};

static const char *const label_fan_current[] = {
	"Fan 1 current",
	"Fan 2 current",
	"Fan 3 current",
	"Fan 4 current",
	"Fan 5 current",
	"Fan 6 current",
	"Fan 7 current",
	"Fan 8 current"
};

struct aqc_data {
	struct hid_device *hdev;
	struct device *hwmon_dev;
	struct dentry *debugfs;
	struct mutex mutex;	/* Used for locking access when reading and writing PWM values */
	enum kinds kind;
	const char *name;

	int buffer_size;
	u8 *buffer;
	int checksum_start;
	int checksum_length;
	int checksum_offset;

	/* General info, same across all devices */
	u32 serial_number[2];
	u16 firmware_version;

	/* How many times the device was powered on */
	u32 power_cycles;

	/* Sensor values */
	s32 temp_input[4];
	u16 speed_input[8];
	u32 power_input[8];
	u16 voltage_input[8];
	u16 current_input[8];

	/* Label values */
	const char *const *temp_label;
	const char *const *speed_label;
	const char *const *power_label;
	const char *const *voltage_label;
	const char *const *current_label;

	unsigned long updated;
};

/* Converts from centi-percent */
static int aqc_percent_to_pwm(u16 val)
{
	return DIV_ROUND_CLOSEST(val * 255, 100 * 100);
}

/* Converts to centi-percent */
static int aqc_pwm_to_percent(long val)
{
	if (val < 0 || val > 255)
		return -EINVAL;

	return DIV_ROUND_CLOSEST(val * 100 * 100, 255);
}

/* Expects the mutex to be locked */
static int aqc_get_ctrl_data(struct aqc_data *priv)
{
	int ret;

	memset(priv->buffer, 0x00, priv->buffer_size);
	ret = hid_hw_raw_request(priv->hdev, CTRL_REPORT_ID, priv->buffer, priv->buffer_size,
				 HID_FEATURE_REPORT, HID_REQ_GET_REPORT);
	if (ret < 0)
		ret = -ENODATA;

	return ret;
}

/* Expects the mutex to be locked */
static int aqc_send_ctrl_data(struct aqc_data *priv)
{
	int ret;
	u16 checksum;

	/* Init and xorout value for CRC-16/USB is 0xffff */
	checksum = crc16(0xffff, priv->buffer + priv->checksum_start, priv->checksum_length);
	checksum ^= 0xffff;

	/* Place the new checksum at the end of the report */
	put_unaligned_be16(checksum, priv->buffer + priv->checksum_offset);

	/* Send the patched up report back to the device */
	ret = hid_hw_raw_request(priv->hdev, CTRL_REPORT_ID, priv->buffer, priv->buffer_size,
				 HID_FEATURE_REPORT, HID_REQ_SET_REPORT);
	if (ret < 0)
		return ret;

	/* The official software sends this report after every change, so do it here as well */
	ret = hid_hw_raw_request(priv->hdev, SECONDARY_CTRL_REPORT_ID, secondary_ctrl_report,
				 SECONDARY_CTRL_REPORT_SIZE, HID_FEATURE_REPORT,
				 HID_REQ_SET_REPORT);
	return ret;
}

/* Refreshes the control buffer and returns value at offset */
static int aqc_get_ctrl_val(struct aqc_data *priv, int offset)
{
	int ret;

	mutex_lock(&priv->mutex);

	ret = aqc_get_ctrl_data(priv);
	if (ret < 0)
		goto unlock_and_return;

	ret = get_unaligned_be16(priv->buffer + offset);

unlock_and_return:
	mutex_unlock(&priv->mutex);
	return ret;
}

static int aqc_set_ctrl_val(struct aqc_data *priv, int offset, long val)
{
	int ret;

	mutex_lock(&priv->mutex);

	ret = aqc_get_ctrl_data(priv);
	if (ret < 0)
		goto unlock_and_return;

	put_unaligned_be16((u16)val, priv->buffer + offset);

	ret = aqc_send_ctrl_data(priv);

unlock_and_return:
	mutex_unlock(&priv->mutex);
	return ret;
}

static umode_t aqc_is_visible(const void *data, enum hwmon_sensor_types type, u32 attr, int channel)
{
	const struct aqc_data *priv = data;

	switch (type) {
	case hwmon_temp:
		switch (priv->kind) {
		case d5next:
			if (channel == 0)
				return 0444;
			break;
		case farbwerk360:
		case octo:
			return 0444;
		default:
			break;
		}
		break;
	case hwmon_pwm:
		switch (priv->kind) {
		case octo:
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
		break;
	case hwmon_fan:
	case hwmon_power:
	case hwmon_curr:
		switch (priv->kind) {
		case d5next:
			if (channel < 2)
				return 0444;
			break;
		case octo:
			return 0444;
		default:
			break;
		}
		break;
	case hwmon_in:
		switch (priv->kind) {
		case d5next:
			if (channel < 3)
				return 0444;
			break;
		case octo:
			return 0444;
		default:
			break;
		}
		break;
	default:
		break;
	}

	return 0;
}

static int aqc_read(struct device *dev, enum hwmon_sensor_types type, u32 attr,
		    int channel, long *val)
{
	int ret;
	struct aqc_data *priv = dev_get_drvdata(dev);

	if (time_after(jiffies, priv->updated + STATUS_UPDATE_INTERVAL))
		return -ENODATA;

	switch (type) {
	case hwmon_temp:
		if (priv->temp_input[channel] == -ENODATA)
			return -ENODATA;

		*val = priv->temp_input[channel];
		break;
	case hwmon_fan:
		*val = priv->speed_input[channel];
		break;
	case hwmon_power:
		*val = priv->power_input[channel];
		break;
	case hwmon_pwm:
		switch (priv->kind) {
		case octo:
			ret = aqc_get_ctrl_val(priv, octo_ctrl_fan_offsets[channel]);
			if (ret < 0)
				return ret;

			*val = aqc_percent_to_pwm(ret);
			break;
		default:
			break;
		}
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

static int aqc_read_string(struct device *dev, enum hwmon_sensor_types type, u32 attr,
			   int channel, const char **str)
{
	struct aqc_data *priv = dev_get_drvdata(dev);

	switch (type) {
	case hwmon_temp:
		*str = priv->temp_label[channel];
		break;
	case hwmon_fan:
		*str = priv->speed_label[channel];
		break;
	case hwmon_power:
		*str = priv->power_label[channel];
		break;
	case hwmon_in:
		*str = priv->voltage_label[channel];
		break;
	case hwmon_curr:
		*str = priv->current_label[channel];
		break;
	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

static int aqc_write(struct device *dev, enum hwmon_sensor_types type, u32 attr, int channel,
		     long val)
{
	int ret, pwm_value;
	struct aqc_data *priv = dev_get_drvdata(dev);

	switch (type) {
	case hwmon_pwm:
		switch (attr) {
		case hwmon_pwm_input:
			switch (priv->kind) {
			case octo:
				pwm_value = aqc_pwm_to_percent(val);
				if (pwm_value < 0)
					return pwm_value;

				ret = aqc_set_ctrl_val(priv, octo_ctrl_fan_offsets[channel],
						       pwm_value);
				if (ret < 0)
					return ret;
				break;
			default:
				break;
			}
			break;
		default:
			break;
		}
		break;
	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

static const struct hwmon_ops aqc_hwmon_ops = {
	.is_visible = aqc_is_visible,
	.read = aqc_read,
	.read_string = aqc_read_string,
	.write = aqc_write
};

static const struct hwmon_channel_info *aqc_info[] = {
	HWMON_CHANNEL_INFO(temp,
			   HWMON_T_INPUT | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_LABEL),
	HWMON_CHANNEL_INFO(fan,
			   HWMON_F_INPUT | HWMON_F_LABEL,
			   HWMON_F_INPUT | HWMON_F_LABEL,
			   HWMON_F_INPUT | HWMON_F_LABEL,
			   HWMON_F_INPUT | HWMON_F_LABEL,
			   HWMON_F_INPUT | HWMON_F_LABEL,
			   HWMON_F_INPUT | HWMON_F_LABEL,
			   HWMON_F_INPUT | HWMON_F_LABEL,
			   HWMON_F_INPUT | HWMON_F_LABEL),
	HWMON_CHANNEL_INFO(power,
			   HWMON_P_INPUT | HWMON_P_LABEL,
			   HWMON_P_INPUT | HWMON_P_LABEL,
			   HWMON_P_INPUT | HWMON_P_LABEL,
			   HWMON_P_INPUT | HWMON_P_LABEL,
			   HWMON_P_INPUT | HWMON_P_LABEL,
			   HWMON_P_INPUT | HWMON_P_LABEL,
			   HWMON_P_INPUT | HWMON_P_LABEL,
			   HWMON_P_INPUT | HWMON_P_LABEL),
	HWMON_CHANNEL_INFO(pwm,
			   HWMON_PWM_INPUT,
			   HWMON_PWM_INPUT,
			   HWMON_PWM_INPUT,
			   HWMON_PWM_INPUT,
			   HWMON_PWM_INPUT,
			   HWMON_PWM_INPUT,
			   HWMON_PWM_INPUT,
			   HWMON_PWM_INPUT),
	HWMON_CHANNEL_INFO(in,
			   HWMON_I_INPUT | HWMON_I_LABEL,
			   HWMON_I_INPUT | HWMON_I_LABEL,
			   HWMON_I_INPUT | HWMON_I_LABEL,
			   HWMON_I_INPUT | HWMON_I_LABEL,
			   HWMON_I_INPUT | HWMON_I_LABEL,
			   HWMON_I_INPUT | HWMON_I_LABEL,
			   HWMON_I_INPUT | HWMON_I_LABEL,
			   HWMON_I_INPUT | HWMON_I_LABEL),
	HWMON_CHANNEL_INFO(curr,
			   HWMON_C_INPUT | HWMON_C_LABEL,
			   HWMON_C_INPUT | HWMON_C_LABEL,
			   HWMON_C_INPUT | HWMON_C_LABEL,
			   HWMON_C_INPUT | HWMON_C_LABEL,
			   HWMON_C_INPUT | HWMON_C_LABEL,
			   HWMON_C_INPUT | HWMON_C_LABEL,
			   HWMON_C_INPUT | HWMON_C_LABEL,
			   HWMON_C_INPUT | HWMON_C_LABEL),
	NULL
};

static const struct hwmon_chip_info aqc_chip_info = {
	.ops = &aqc_hwmon_ops,
	.info = aqc_info,
};

static int aqc_raw_event(struct hid_device *hdev, struct hid_report *report, u8 *data,
			 int size)
{
	int i, sensor_value;
	struct aqc_data *priv;

	if (report->id != STATUS_REPORT_ID)
		return 0;

	priv = hid_get_drvdata(hdev);

	/* Info provided with every report */
	priv->serial_number[0] = get_unaligned_be16(data + SERIAL_FIRST_PART);
	priv->serial_number[1] = get_unaligned_be16(data + SERIAL_SECOND_PART);
	priv->firmware_version = get_unaligned_be16(data + FIRMWARE_VERSION);

	/* Sensor readings */
	switch (priv->kind) {
	case d5next:
		priv->power_cycles = get_unaligned_be32(data + D5NEXT_POWER_CYCLES);

		priv->temp_input[0] = get_unaligned_be16(data + D5NEXT_COOLANT_TEMP) * 10;

		priv->speed_input[0] = get_unaligned_be16(data + D5NEXT_PUMP_SPEED);
		priv->speed_input[1] = get_unaligned_be16(data + D5NEXT_FAN_SPEED);

		priv->power_input[0] = get_unaligned_be16(data + D5NEXT_PUMP_POWER) * 10000;
		priv->power_input[1] = get_unaligned_be16(data + D5NEXT_FAN_POWER) * 10000;

		priv->voltage_input[0] = get_unaligned_be16(data + D5NEXT_PUMP_VOLTAGE) * 10;
		priv->voltage_input[1] = get_unaligned_be16(data + D5NEXT_FAN_VOLTAGE) * 10;
		priv->voltage_input[2] = get_unaligned_be16(data + D5NEXT_5V_VOLTAGE) * 10;

		priv->current_input[0] = get_unaligned_be16(data + D5NEXT_PUMP_CURRENT);
		priv->current_input[1] = get_unaligned_be16(data + D5NEXT_FAN_CURRENT);
		break;
	case farbwerk360:
		/* Temperature sensor readings */
		for (i = 0; i < FARBWERK360_NUM_SENSORS; i++) {
			sensor_value = get_unaligned_be16(data + FARBWERK360_SENSOR_START +
							  i * FARBWERK360_SENSOR_SIZE);
			if (sensor_value == FARBWERK360_SENSOR_DISCONNECTED)
				priv->temp_input[i] = -ENODATA;
			else
				priv->temp_input[i] = sensor_value * 10;
		}
		break;
	case octo:
		priv->power_cycles = get_unaligned_be32(data + OCTO_POWER_CYCLES);

		/* Fan speed and related readings */
		for (i = 0; i < OCTO_NUM_FANS; i++) {
			priv->speed_input[i] =
			    get_unaligned_be16(data + octo_sensor_fan_offsets[i] +
					       OCTO_FAN_SPEED_OFFSET);
			priv->power_input[i] =
			    get_unaligned_be16(data + octo_sensor_fan_offsets[i] +
					       OCTO_FAN_POWER_OFFSET) * 10000;
			priv->voltage_input[i] =
			    get_unaligned_be16(data + octo_sensor_fan_offsets[i] +
					       OCTO_FAN_VOLTAGE_OFFSET) * 10;
			priv->current_input[i] =
			    get_unaligned_be16(data + octo_sensor_fan_offsets[i] +
					       OCTO_FAN_CURRENT_OFFSET);
		}

		/* Temperature sensor readings */
		for (i = 0; i < OCTO_NUM_SENSORS; i++) {
			sensor_value = get_unaligned_be16(data + OCTO_SENSOR_START +
							  i * OCTO_SENSOR_SIZE);
			if (sensor_value == OCTO_SENSOR_DISCONNECTED)
				priv->temp_input[i] = -ENODATA;
			else
				priv->temp_input[i] = sensor_value * 10;
		}
		break;
	default:
		break;
	}

	priv->updated = jiffies;

	return 0;
}

#ifdef CONFIG_DEBUG_FS

static int serial_number_show(struct seq_file *seqf, void *unused)
{
	struct aqc_data *priv = seqf->private;

	seq_printf(seqf, "%05u-%05u\n", priv->serial_number[0], priv->serial_number[1]);

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(serial_number);

static int firmware_version_show(struct seq_file *seqf, void *unused)
{
	struct aqc_data *priv = seqf->private;

	seq_printf(seqf, "%u\n", priv->firmware_version);

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(firmware_version);

static int power_cycles_show(struct seq_file *seqf, void *unused)
{
	struct aqc_data *priv = seqf->private;

	seq_printf(seqf, "%u\n", priv->power_cycles);

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(power_cycles);

static void aqc_debugfs_init(struct aqc_data *priv)
{
	char name[64];

	scnprintf(name, sizeof(name), "%s_%s-%s", "aquacomputer", priv->name,
		  dev_name(&priv->hdev->dev));

	priv->debugfs = debugfs_create_dir(name, NULL);
	debugfs_create_file("serial_number", 0444, priv->debugfs, priv, &serial_number_fops);
	debugfs_create_file("firmware_version", 0444, priv->debugfs, priv, &firmware_version_fops);

	switch (priv->kind) {
	case d5next:
	case octo:
		debugfs_create_file("power_cycles", 0444, priv->debugfs, priv, &power_cycles_fops);
		break;
	default:
		break;
	}
}

#else

static void aqc_debugfs_init(struct aqc_data *priv)
{
}

#endif

static int aqc_probe(struct hid_device *hdev, const struct hid_device_id *id)
{
	struct aqc_data *priv;
	int ret;

	priv = devm_kzalloc(&hdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->hdev = hdev;
	hid_set_drvdata(hdev, priv);

	priv->updated = jiffies - STATUS_UPDATE_INTERVAL;

	ret = hid_parse(hdev);
	if (ret)
		return ret;

	ret = hid_hw_start(hdev, HID_CONNECT_HIDRAW);
	if (ret)
		return ret;

	ret = hid_hw_open(hdev);
	if (ret)
		goto fail_and_stop;

	switch (hdev->product) {
	case USB_PRODUCT_ID_D5NEXT:
		priv->kind = d5next;

		priv->temp_label = label_d5next_temp;
		priv->speed_label = label_d5next_speeds;
		priv->power_label = label_d5next_power;
		priv->voltage_label = label_d5next_voltages;
		priv->current_label = label_d5next_current;
		break;
	case USB_PRODUCT_ID_FARBWERK360:
		priv->kind = farbwerk360;

		priv->temp_label = label_temp_sensors;
		break;
	case USB_PRODUCT_ID_OCTO:
		priv->kind = octo;
		priv->buffer_size = OCTO_CTRL_REPORT_SIZE;
		priv->checksum_start = OCTO_CTRL_REPORT_CHECKSUM_START;
		priv->checksum_length = OCTO_CTRL_REPORT_CHECKSUM_LENGTH;
		priv->checksum_offset = OCTO_CTRL_REPORT_CHECKSUM_OFFSET;

		priv->temp_label = label_temp_sensors;
		priv->speed_label = label_fan_speed;
		priv->power_label = label_fan_power;
		priv->voltage_label = label_fan_voltage;
		priv->current_label = label_fan_current;
		break;
	default:
		break;
	}

	priv->name = aqc_device_names[priv->kind];

	priv->buffer = devm_kzalloc(&hdev->dev, priv->buffer_size, GFP_KERNEL);
	if (!priv->buffer)
		return -ENOMEM;

	mutex_init(&priv->mutex);

	priv->hwmon_dev = hwmon_device_register_with_info(&hdev->dev, priv->name, priv,
							  &aqc_chip_info, NULL);

	if (IS_ERR(priv->hwmon_dev)) {
		ret = PTR_ERR(priv->hwmon_dev);
		goto fail_and_close;
	}

	aqc_debugfs_init(priv);

	return 0;

fail_and_close:
	hid_hw_close(hdev);
fail_and_stop:
	hid_hw_stop(hdev);
	return ret;
}

static void aqc_remove(struct hid_device *hdev)
{
	struct aqc_data *priv = hid_get_drvdata(hdev);

	debugfs_remove_recursive(priv->debugfs);
	hwmon_device_unregister(priv->hwmon_dev);

	hid_hw_close(hdev);
	hid_hw_stop(hdev);
}

static const struct hid_device_id aqc_table[] = {
	{ HID_USB_DEVICE(USB_VENDOR_ID_AQUACOMPUTER, USB_PRODUCT_ID_D5NEXT) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_AQUACOMPUTER, USB_PRODUCT_ID_FARBWERK360) },
	{ HID_USB_DEVICE(USB_VENDOR_ID_AQUACOMPUTER, USB_PRODUCT_ID_OCTO) },
	{ }
};

MODULE_DEVICE_TABLE(hid, aqc_table);

static struct hid_driver aqc_driver = {
	.name = DRIVER_NAME,
	.id_table = aqc_table,
	.probe = aqc_probe,
	.remove = aqc_remove,
	.raw_event = aqc_raw_event,
};

static int __init aqc_init(void)
{
	return hid_register_driver(&aqc_driver);
}

static void __exit aqc_exit(void)
{
	hid_unregister_driver(&aqc_driver);
}

/* Request to initialize after the HID bus to ensure it's not being loaded before */
late_initcall(aqc_init);
module_exit(aqc_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Aleksa Savic <savicaleksa83@gmail.com>");
MODULE_DESCRIPTION("Hwmon driver for Aquacomputer devices");
