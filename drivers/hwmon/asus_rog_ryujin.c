// SPDX-License-Identifier: GPL-2.0+
/*
 * hwmon driver for Asus ROG Ryujin II 360 AIO cooler.
 *
 * Copyright 2024 Aleksa Savic <savicaleksa83@gmail.com>
 */

#include <linux/debugfs.h>
#include <linux/hid.h>
#include <linux/hwmon.h>
#include <linux/jiffies.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <asm/unaligned.h>

#define DRIVER_NAME	"asus_rog_ryujin"

#define USB_VENDOR_ID_ASUS_ROG		0x0b05
#define USB_PRODUCT_ID_RYUJIN_AIO	0x1988	/* ASUS ROG RYUJIN II 360 */

#define STATUS_VALIDITY		1500	/* ms */
#define MAX_REPORT_LENGTH	65

/* Cooler status report offsets */
#define RYUJIN_TEMP_SENSOR_1		3
#define RYUJIN_TEMP_SENSOR_2		4
#define RYUJIN_PUMP_SPEED		5
#define RYUJIN_INTERNAL_FAN_SPEED	7

/* Cooler duty report offsets */
#define RYUJIN_PUMP_DUTY		4
#define RYUJIN_INTERNAL_FAN_DUTY	5

/* Controller status (speeds) report offsets */
#define RYUJIN_CONTROLLER_SPEED_1	5
#define RYUJIN_CONTROLLER_SPEED_2	7
#define RYUJIN_CONTROLLER_SPEED_3	9
#define RYUJIN_CONTROLLER_SPEED_4	3

/* Controller duty report offsets */
#define RYUJIN_CONTROLLER_DUTY		4

/* Control commands and their inner offsets */
#define RYUJIN_CMD_PREFIX	0xEC

static const u8 get_cooler_status_cmd[] = { RYUJIN_CMD_PREFIX, 0x99 };
static const u8 get_cooler_duty_cmd[] = { RYUJIN_CMD_PREFIX, 0x9A };
static const u8 get_controller_speed_cmd[] = { RYUJIN_CMD_PREFIX, 0xA0 };
static const u8 get_controller_duty_cmd[] = { RYUJIN_CMD_PREFIX, 0xA1 };

#define RYUJIN_SET_COOLER_PUMP_DUTY_OFFSET	3
#define RYUJIN_SET_COOLER_FAN_DUTY_OFFSET	4
static const u8 set_cooler_duty_cmd[] = { RYUJIN_CMD_PREFIX, 0x1A, 0x00, 0x00, 0x00 };

#define RYUJIN_SET_CONTROLLER_FAN_DUTY_OFFSET	4
static const u8 set_controller_duty_cmd[] = { RYUJIN_CMD_PREFIX, 0x21, 0x00, 0x00, 0x00 };

/* Command lengths */
#define GET_CMD_LENGTH	2	/* Same length for all get commands */
#define SET_CMD_LENGTH	5	/* Same length for all set commands */

/* Command response headers */
#define RYUJIN_GET_COOLER_STATUS_CMD_RESPONSE		0x19
#define RYUJIN_GET_COOLER_DUTY_CMD_RESPONSE		0x1A
#define RYUJIN_GET_CONTROLLER_SPEED_CMD_RESPONSE	0x20
#define RYUJIN_GET_CONTROLLER_DUTY_CMD_RESPONSE		0x21

static const char *const rog_ryujin_temp_label[] = {
	"Coolant temp"
};

static const char *const rog_ryujin_speed_label[] = {
	"Pump speed",
	"Internal fan speed",
	"Controller fan 1 speed",
	"Controller fan 2 speed",
	"Controller fan 3 speed",
	"Controller fan 4 speed",
};

struct rog_ryujin_data {
	struct hid_device *hdev;
	struct device *hwmon_dev;
	/* For locking access to buffer */
	struct mutex buffer_lock;
	/* For queueing multiple readers */
	struct mutex status_report_request_mutex;
	/* For reinitializing the completions below */
	spinlock_t status_report_request_lock;
	struct completion cooler_status_received;
	struct completion controller_status_received;
	struct completion cooler_duty_received;
	struct completion controller_duty_received;
	struct completion cooler_duty_set;
	struct completion controller_duty_set;

	/* Sensor data */
	s32 temp_input[1];
	u16 speed_input[6];	/* Pump, internal fan and four controller fan speeds in RPM */
	u8 duty_input[3];	/* Pump, internal fan and controller fan duty in PWM */

	u8 *buffer;
	unsigned long updated;	/* jiffies */
};

static int rog_ryujin_percent_to_pwm(u16 val)
{
	return DIV_ROUND_CLOSEST(val * 255, 100);
}

static int rog_ryujin_pwm_to_percent(long val)
{
	return DIV_ROUND_CLOSEST(val * 100, 255);
}

static umode_t rog_ryujin_is_visible(const void *data,
				     enum hwmon_sensor_types type, u32 attr, int channel)
{
	switch (type) {
	case hwmon_temp:
		switch (attr) {
		case hwmon_temp_label:
		case hwmon_temp_input:
			return 0444;
		default:
			break;
		}
		break;
	case hwmon_fan:
		switch (attr) {
		case hwmon_fan_label:
		case hwmon_fan_input:
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

/* Writes the command to the device with the rest of the report filled with zeroes */
static int rog_ryujin_write_expanded(struct rog_ryujin_data *priv, const u8 *cmd, int cmd_length)
{
	int ret;

	mutex_lock(&priv->buffer_lock);

	memcpy_and_pad(priv->buffer, MAX_REPORT_LENGTH, cmd, cmd_length, 0x00);
	ret = hid_hw_output_report(priv->hdev, priv->buffer, MAX_REPORT_LENGTH);

	mutex_unlock(&priv->buffer_lock);
	return ret;
}

/* Assumes priv->status_report_request_mutex is locked */
static int rog_ryujin_execute_cmd(struct rog_ryujin_data *priv, const u8 *cmd, int cmd_length,
				  struct completion *status_completion)
{
	int ret;

	/*
	 * Disable raw event parsing for a moment to safely reinitialize the
	 * completion. Reinit is done because hidraw could have triggered
	 * the raw event parsing and marked the passed in completion as done.
	 */
	spin_lock_bh(&priv->status_report_request_lock);
	reinit_completion(status_completion);
	spin_unlock_bh(&priv->status_report_request_lock);

	/* Send command for getting data */
	ret = rog_ryujin_write_expanded(priv, cmd, cmd_length);
	if (ret < 0)
		return ret;

	ret = wait_for_completion_interruptible_timeout(status_completion,
							msecs_to_jiffies(STATUS_VALIDITY));
	if (ret == 0)
		return -ETIMEDOUT;
	else if (ret < 0)
		return ret;

	return 0;
}

static int rog_ryujin_get_status(struct rog_ryujin_data *priv)
{
	int ret = mutex_lock_interruptible(&priv->status_report_request_mutex);

	if (ret < 0)
		return ret;

	if (!time_after(jiffies, priv->updated + msecs_to_jiffies(STATUS_VALIDITY))) {
		/* Data is up to date */
		goto unlock_and_return;
	}

	/* Retrieve cooler status */
	ret =
	    rog_ryujin_execute_cmd(priv, get_cooler_status_cmd, GET_CMD_LENGTH,
				   &priv->cooler_status_received);
	if (ret < 0)
		goto unlock_and_return;

	/* Retrieve controller status (speeds) */
	ret =
	    rog_ryujin_execute_cmd(priv, get_controller_speed_cmd, GET_CMD_LENGTH,
				   &priv->controller_status_received);
	if (ret < 0)
		goto unlock_and_return;

	/* Retrieve cooler duty */
	ret =
	    rog_ryujin_execute_cmd(priv, get_cooler_duty_cmd, GET_CMD_LENGTH,
				   &priv->cooler_duty_received);
	if (ret < 0)
		goto unlock_and_return;

	/* Retrieve controller duty */
	ret =
	    rog_ryujin_execute_cmd(priv, get_controller_duty_cmd, GET_CMD_LENGTH,
				   &priv->controller_duty_received);
	if (ret < 0)
		goto unlock_and_return;

	priv->updated = jiffies;

unlock_and_return:
	mutex_unlock(&priv->status_report_request_mutex);
	if (ret < 0)
		return ret;

	return 0;
}

static int rog_ryujin_read(struct device *dev, enum hwmon_sensor_types type,
			   u32 attr, int channel, long *val)
{
	struct rog_ryujin_data *priv = dev_get_drvdata(dev);
	int ret = rog_ryujin_get_status(priv);

	if (ret < 0)
		return ret;

	switch (type) {
	case hwmon_temp:
		*val = priv->temp_input[channel];
		break;
	case hwmon_fan:
		*val = priv->speed_input[channel];
		break;
	case hwmon_pwm:
		switch (attr) {
		case hwmon_pwm_input:
			*val = priv->duty_input[channel];
			break;
		default:
			return -EOPNOTSUPP;
		}
		break;
	default:
		return -EOPNOTSUPP;	/* unreachable */
	}

	return 0;
}

static int rog_ryujin_read_string(struct device *dev, enum hwmon_sensor_types type,
				  u32 attr, int channel, const char **str)
{
	switch (type) {
	case hwmon_temp:
		*str = rog_ryujin_temp_label[channel];
		break;
	case hwmon_fan:
		*str = rog_ryujin_speed_label[channel];
		break;
	default:
		return -EOPNOTSUPP;	/* unreachable */
	}

	return 0;
}

static int rog_ryujin_write_fixed_duty(struct rog_ryujin_data *priv, int channel, int val)
{
	u8 set_cmd[SET_CMD_LENGTH];
	int ret;

	if (channel < 2) {
		/*
		 * Retrieve cooler duty since both pump and internal fan are set
		 * together, then write back with one of them modified.
		 */
		ret = mutex_lock_interruptible(&priv->status_report_request_mutex);
		if (ret < 0)
			return ret;
		ret =
		    rog_ryujin_execute_cmd(priv, get_cooler_duty_cmd, GET_CMD_LENGTH,
					   &priv->cooler_duty_received);
		if (ret < 0)
			goto unlock_and_return;

		memcpy(set_cmd, set_cooler_duty_cmd, SET_CMD_LENGTH);

		/* Cooler duties are set as 0-100% */
		val = rog_ryujin_pwm_to_percent(val);

		if (channel == 0) {
			/* Cooler pump duty */
			set_cmd[RYUJIN_SET_COOLER_PUMP_DUTY_OFFSET] = val;
			set_cmd[RYUJIN_SET_COOLER_FAN_DUTY_OFFSET] =
			    rog_ryujin_pwm_to_percent(priv->duty_input[1]);
		} else if (channel == 1) {
			/* Cooler internal fan duty */
			set_cmd[RYUJIN_SET_COOLER_PUMP_DUTY_OFFSET] =
			    rog_ryujin_pwm_to_percent(priv->duty_input[0]);
			set_cmd[RYUJIN_SET_COOLER_FAN_DUTY_OFFSET] = val;
		}

		ret = rog_ryujin_execute_cmd(priv, set_cmd, SET_CMD_LENGTH, &priv->cooler_duty_set);
unlock_and_return:
		mutex_unlock(&priv->status_report_request_mutex);
		if (ret < 0)
			return ret;
	} else {
		/*
		 * Controller fan duty (channel == 2). No need to retrieve current
		 * duty, so just send the command.
		 */
		memcpy(set_cmd, set_controller_duty_cmd, SET_CMD_LENGTH);
		set_cmd[RYUJIN_SET_CONTROLLER_FAN_DUTY_OFFSET] = val;

		ret =
		    rog_ryujin_execute_cmd(priv, set_cmd, SET_CMD_LENGTH,
					   &priv->controller_duty_set);
		if (ret < 0)
			return ret;
	}

	/* Lock onto this value until next refresh cycle */
	priv->duty_input[channel] = val;

	return 0;
}

static int rog_ryujin_write(struct device *dev, enum hwmon_sensor_types type, u32 attr, int channel,
			    long val)
{
	struct rog_ryujin_data *priv = dev_get_drvdata(dev);
	int ret;

	switch (type) {
	case hwmon_pwm:
		switch (attr) {
		case hwmon_pwm_input:
			if (val < 0 || val > 255)
				return -EINVAL;

			ret = rog_ryujin_write_fixed_duty(priv, channel, val);
			if (ret < 0)
				return ret;
			break;
		default:
			return -EOPNOTSUPP;
		}
		break;
	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

static const struct hwmon_ops rog_ryujin_hwmon_ops = {
	.is_visible = rog_ryujin_is_visible,
	.read = rog_ryujin_read,
	.read_string = rog_ryujin_read_string,
	.write = rog_ryujin_write
};

static const struct hwmon_channel_info *rog_ryujin_info[] = {
	HWMON_CHANNEL_INFO(temp,
			   HWMON_T_INPUT | HWMON_T_LABEL),
	HWMON_CHANNEL_INFO(fan,
			   HWMON_F_INPUT | HWMON_F_LABEL,
			   HWMON_F_INPUT | HWMON_F_LABEL,
			   HWMON_F_INPUT | HWMON_F_LABEL,
			   HWMON_F_INPUT | HWMON_F_LABEL,
			   HWMON_F_INPUT | HWMON_F_LABEL,
			   HWMON_F_INPUT | HWMON_F_LABEL),
	HWMON_CHANNEL_INFO(pwm,
			   HWMON_PWM_INPUT,
			   HWMON_PWM_INPUT,
			   HWMON_PWM_INPUT),
	NULL
};

static const struct hwmon_chip_info rog_ryujin_chip_info = {
	.ops = &rog_ryujin_hwmon_ops,
	.info = rog_ryujin_info,
};

static int rog_ryujin_raw_event(struct hid_device *hdev, struct hid_report *report, u8 *data,
				int size)
{
	struct rog_ryujin_data *priv = hid_get_drvdata(hdev);

	if (data[0] != RYUJIN_CMD_PREFIX)
		return 0;

	if (data[1] == RYUJIN_GET_COOLER_STATUS_CMD_RESPONSE) {
		/* Received coolant temp and speeds of pump and internal fan */
		priv->temp_input[0] =
		    data[RYUJIN_TEMP_SENSOR_1] * 1000 + data[RYUJIN_TEMP_SENSOR_2] * 100;
		priv->speed_input[0] = get_unaligned_le16(data + RYUJIN_PUMP_SPEED);
		priv->speed_input[1] = get_unaligned_le16(data + RYUJIN_INTERNAL_FAN_SPEED);

		if (!completion_done(&priv->cooler_status_received))
			complete_all(&priv->cooler_status_received);
	} else if (data[1] == RYUJIN_GET_CONTROLLER_SPEED_CMD_RESPONSE) {
		/* Received speeds of four fans attached to the controller */
		priv->speed_input[2] = get_unaligned_le16(data + RYUJIN_CONTROLLER_SPEED_1);
		priv->speed_input[3] = get_unaligned_le16(data + RYUJIN_CONTROLLER_SPEED_2);
		priv->speed_input[4] = get_unaligned_le16(data + RYUJIN_CONTROLLER_SPEED_3);
		priv->speed_input[5] = get_unaligned_le16(data + RYUJIN_CONTROLLER_SPEED_4);

		if (!completion_done(&priv->controller_status_received))
			complete_all(&priv->controller_status_received);
	} else if (data[1] == RYUJIN_GET_COOLER_DUTY_CMD_RESPONSE) {
		/* Received report for pump and internal fan duties (in %) */
		if (data[RYUJIN_PUMP_DUTY] == 0 && data[RYUJIN_INTERNAL_FAN_DUTY] == 0) {
			/*
			 * We received a report with zeroes for duty in both places.
			 * The device returns this as a confirmation that setting values
			 * is successful. If we initiated a write, mark it as complete.
			 */
			if (!completion_done(&priv->cooler_duty_set))
				complete_all(&priv->cooler_duty_set);
			else if (!completion_done(&priv->cooler_duty_received))
				/*
				 * We didn't initiate a write, but received both zeroes.
				 * This means that either both duties are actually zero,
				 * or that we received a success report caused by userspace.
				 * We're expecting a report, so parse it.
				 */
				goto read_cooler_duty;
			return 0;
		}
read_cooler_duty:
		priv->duty_input[0] = rog_ryujin_percent_to_pwm(data[RYUJIN_PUMP_DUTY]);
		priv->duty_input[1] = rog_ryujin_percent_to_pwm(data[RYUJIN_INTERNAL_FAN_DUTY]);

		if (!completion_done(&priv->cooler_duty_received))
			complete_all(&priv->cooler_duty_received);
	} else if (data[1] == RYUJIN_GET_CONTROLLER_DUTY_CMD_RESPONSE) {
		/* Received report for controller duty for fans (in PWM) */
		if (data[RYUJIN_CONTROLLER_DUTY] == 0) {
			/*
			 * We received a report with a zero for duty. The device returns this as
			 * a confirmation that setting the controller duty value was successful.
			 * If we initiated a write, mark it as complete.
			 */
			if (!completion_done(&priv->controller_duty_set))
				complete_all(&priv->controller_duty_set);
			else if (!completion_done(&priv->controller_duty_received))
				/*
				 * We didn't initiate a write, but received a zero for duty.
				 * This means that either the duty is actually zero, or that
				 * we received a success report caused by userspace.
				 * We're expecting a report, so parse it.
				 */
				goto read_controller_duty;
			return 0;
		}
read_controller_duty:
		priv->duty_input[2] = data[RYUJIN_CONTROLLER_DUTY];

		if (!completion_done(&priv->controller_duty_received))
			complete_all(&priv->controller_duty_received);
	}

	return 0;
}

static int rog_ryujin_probe(struct hid_device *hdev, const struct hid_device_id *id)
{
	struct rog_ryujin_data *priv;
	int ret;

	priv = devm_kzalloc(&hdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->hdev = hdev;
	hid_set_drvdata(hdev, priv);

	/*
	 * Initialize priv->updated to STATUS_VALIDITY seconds in the past, making
	 * the initial empty data invalid for rog_ryujin_read() without the need for
	 * a special case there.
	 */
	priv->updated = jiffies - msecs_to_jiffies(STATUS_VALIDITY);

	ret = hid_parse(hdev);
	if (ret) {
		hid_err(hdev, "hid parse failed with %d\n", ret);
		return ret;
	}

	/* Enable hidraw so existing user-space tools can continue to work */
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

	priv->buffer = devm_kzalloc(&hdev->dev, MAX_REPORT_LENGTH, GFP_KERNEL);
	if (!priv->buffer) {
		ret = -ENOMEM;
		goto fail_and_close;
	}

	mutex_init(&priv->status_report_request_mutex);
	mutex_init(&priv->buffer_lock);
	spin_lock_init(&priv->status_report_request_lock);
	init_completion(&priv->cooler_status_received);
	init_completion(&priv->controller_status_received);
	init_completion(&priv->cooler_duty_received);
	init_completion(&priv->controller_duty_received);
	init_completion(&priv->cooler_duty_set);
	init_completion(&priv->controller_duty_set);

	priv->hwmon_dev = hwmon_device_register_with_info(&hdev->dev, "rog_ryujin",
							  priv, &rog_ryujin_chip_info, NULL);
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

static void rog_ryujin_remove(struct hid_device *hdev)
{
	struct rog_ryujin_data *priv = hid_get_drvdata(hdev);

	hwmon_device_unregister(priv->hwmon_dev);

	hid_hw_close(hdev);
	hid_hw_stop(hdev);
}

static const struct hid_device_id rog_ryujin_table[] = {
	{ HID_USB_DEVICE(USB_VENDOR_ID_ASUS_ROG, USB_PRODUCT_ID_RYUJIN_AIO) },
	{ }
};

MODULE_DEVICE_TABLE(hid, rog_ryujin_table);

static struct hid_driver rog_ryujin_driver = {
	.name = "rog_ryujin",
	.id_table = rog_ryujin_table,
	.probe = rog_ryujin_probe,
	.remove = rog_ryujin_remove,
	.raw_event = rog_ryujin_raw_event,
};

static int __init rog_ryujin_init(void)
{
	return hid_register_driver(&rog_ryujin_driver);
}

static void __exit rog_ryujin_exit(void)
{
	hid_unregister_driver(&rog_ryujin_driver);
}

/* When compiled into the kernel, initialize after the HID bus */
late_initcall(rog_ryujin_init);
module_exit(rog_ryujin_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Aleksa Savic <savicaleksa83@gmail.com>");
MODULE_DESCRIPTION("Hwmon driver for Asus ROG Ryujin II 360 AIO cooler");
