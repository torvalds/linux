// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Reverse-engineered NZXT RGB & Fan Controller/Smart Device v2 driver.
 *
 * Copyright (c) 2021 Aleksandr Mezin
 */

#include <linux/hid.h>
#include <linux/hwmon.h>
#include <linux/math.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/wait.h>

#include <asm/byteorder.h>
#include <linux/unaligned.h>

/*
 * The device has only 3 fan channels/connectors. But all HID reports have
 * space reserved for up to 8 channels.
 */
#define FAN_CHANNELS 3
#define FAN_CHANNELS_MAX 8

#define UPDATE_INTERVAL_DEFAULT_MS 1000

/* These strings match labels on the device exactly */
static const char *const fan_label[] = {
	"FAN 1",
	"FAN 2",
	"FAN 3",
};

static const char *const curr_label[] = {
	"FAN 1 Current",
	"FAN 2 Current",
	"FAN 3 Current",
};

static const char *const in_label[] = {
	"FAN 1 Voltage",
	"FAN 2 Voltage",
	"FAN 3 Voltage",
};

enum {
	INPUT_REPORT_ID_FAN_CONFIG = 0x61,
	INPUT_REPORT_ID_FAN_STATUS = 0x67,
};

enum {
	FAN_STATUS_REPORT_SPEED = 0x02,
	FAN_STATUS_REPORT_VOLTAGE = 0x04,
};

enum {
	FAN_TYPE_NONE = 0,
	FAN_TYPE_DC = 1,
	FAN_TYPE_PWM = 2,
};

struct unknown_static_data {
	/*
	 * Some configuration data? Stays the same after fan speed changes,
	 * changes in fan configuration, reboots and driver reloads.
	 *
	 * The same data in multiple report types.
	 *
	 * Byte 12 seems to be the number of fan channels, but I am not sure.
	 */
	u8 unknown1[14];
} __packed;

/*
 * The device sends this input report in response to "detect fans" command:
 * a 2-byte output report { 0x60, 0x03 }.
 */
struct fan_config_report {
	/* report_id should be INPUT_REPORT_ID_FAN_CONFIG = 0x61 */
	u8 report_id;
	/* Always 0x03 */
	u8 magic;
	struct unknown_static_data unknown_data;
	/* Fan type as detected by the device. See FAN_TYPE_* enum. */
	u8 fan_type[FAN_CHANNELS_MAX];
} __packed;

/*
 * The device sends these reports at a fixed interval (update interval) -
 * one report with type = FAN_STATUS_REPORT_SPEED, and one report with type =
 * FAN_STATUS_REPORT_VOLTAGE per update interval.
 */
struct fan_status_report {
	/* report_id should be INPUT_REPORT_ID_STATUS = 0x67 */
	u8 report_id;
	/* FAN_STATUS_REPORT_SPEED = 0x02 or FAN_STATUS_REPORT_VOLTAGE = 0x04 */
	u8 type;
	struct unknown_static_data unknown_data;
	/* Fan type as detected by the device. See FAN_TYPE_* enum. */
	u8 fan_type[FAN_CHANNELS_MAX];

	union {
		/* When type == FAN_STATUS_REPORT_SPEED */
		struct {
			/*
			 * Fan speed, in RPM. Zero for channels without fans
			 * connected.
			 */
			__le16 fan_rpm[FAN_CHANNELS_MAX];
			/*
			 * Fan duty cycle, in percent. Non-zero even for
			 * channels without fans connected.
			 */
			u8 duty_percent[FAN_CHANNELS_MAX];
			/*
			 * Exactly the same values as duty_percent[], non-zero
			 * for disconnected fans too.
			 */
			u8 duty_percent_dup[FAN_CHANNELS_MAX];
			/* "Case Noise" in db */
			u8 noise_db;
		} __packed fan_speed;
		/* When type == FAN_STATUS_REPORT_VOLTAGE */
		struct {
			/*
			 * Voltage, in millivolts. Non-zero even when fan is
			 * not connected.
			 */
			__le16 fan_in[FAN_CHANNELS_MAX];
			/*
			 * Current, in milliamperes. Near-zero when
			 * disconnected.
			 */
			__le16 fan_current[FAN_CHANNELS_MAX];
		} __packed fan_voltage;
	} __packed;
} __packed;

#define OUTPUT_REPORT_SIZE 64

enum {
	OUTPUT_REPORT_ID_INIT_COMMAND = 0x60,
	OUTPUT_REPORT_ID_SET_FAN_SPEED = 0x62,
};

enum {
	INIT_COMMAND_SET_UPDATE_INTERVAL = 0x02,
	INIT_COMMAND_DETECT_FANS = 0x03,
};

/*
 * This output report sets pwm duty cycle/target fan speed for one or more
 * channels.
 */
struct set_fan_speed_report {
	/* report_id should be OUTPUT_REPORT_ID_SET_FAN_SPEED = 0x62 */
	u8 report_id;
	/* Should be 0x01 */
	u8 magic;
	/* To change fan speed on i-th channel, set i-th bit here */
	u8 channel_bit_mask;
	/*
	 * Fan duty cycle/target speed in percent. For voltage-controlled fans,
	 * the minimal voltage (duty_percent = 1) is about 9V.
	 * Setting duty_percent to 0 (if the channel is selected in
	 * channel_bit_mask) turns off the fan completely (regardless of the
	 * control mode).
	 */
	u8 duty_percent[FAN_CHANNELS_MAX];
} __packed;

struct drvdata {
	struct hid_device *hid;
	struct device *hwmon;

	u8 fan_duty_percent[FAN_CHANNELS];
	u16 fan_rpm[FAN_CHANNELS];
	bool pwm_status_received;

	u16 fan_in[FAN_CHANNELS];
	u16 fan_curr[FAN_CHANNELS];
	bool voltage_status_received;

	u8 fan_type[FAN_CHANNELS];
	bool fan_config_received;

	/*
	 * wq is used to wait for *_received flags to become true.
	 * All accesses to *_received flags and fan_* arrays are performed with
	 * wq.lock held.
	 */
	wait_queue_head_t wq;
	/*
	 * mutex is used to:
	 * 1) Prevent concurrent conflicting changes to update interval and pwm
	 * values (after sending an output hid report, the corresponding field
	 * in drvdata must be updated, and only then new output reports can be
	 * sent).
	 * 2) Synchronize access to output_buffer (well, the buffer is here,
	 * because synchronization is necessary anyway - so why not get rid of
	 * a kmalloc?).
	 */
	struct mutex mutex;
	long update_interval;
	u8 output_buffer[OUTPUT_REPORT_SIZE];
};

static long scale_pwm_value(long val, long orig_max, long new_max)
{
	if (val <= 0)
		return 0;

	/*
	 * Positive values should not become zero: 0 completely turns off the
	 * fan.
	 */
	return max(1L, DIV_ROUND_CLOSEST(min(val, orig_max) * new_max, orig_max));
}

static void handle_fan_config_report(struct drvdata *drvdata, void *data, int size)
{
	struct fan_config_report *report = data;
	int i;

	if (size < sizeof(struct fan_config_report))
		return;

	if (report->magic != 0x03)
		return;

	spin_lock(&drvdata->wq.lock);

	for (i = 0; i < FAN_CHANNELS; i++)
		drvdata->fan_type[i] = report->fan_type[i];

	drvdata->fan_config_received = true;
	wake_up_all_locked(&drvdata->wq);
	spin_unlock(&drvdata->wq.lock);
}

static void handle_fan_status_report(struct drvdata *drvdata, void *data, int size)
{
	struct fan_status_report *report = data;
	int i;

	if (size < sizeof(struct fan_status_report))
		return;

	spin_lock(&drvdata->wq.lock);

	/*
	 * The device sends INPUT_REPORT_ID_FAN_CONFIG = 0x61 report in response
	 * to "detect fans" command. Only accept other data after getting 0x61,
	 * to make sure that fan detection is complete. In particular, fan
	 * detection resets pwm values.
	 */
	if (!drvdata->fan_config_received) {
		spin_unlock(&drvdata->wq.lock);
		return;
	}

	for (i = 0; i < FAN_CHANNELS; i++) {
		if (drvdata->fan_type[i] == report->fan_type[i])
			continue;

		/*
		 * This should not happen (if my expectations about the device
		 * are correct).
		 *
		 * Even if the userspace sends fan detect command through
		 * hidraw, fan config report should arrive first.
		 */
		hid_warn_once(drvdata->hid,
			      "Fan %d type changed unexpectedly from %d to %d",
			      i, drvdata->fan_type[i], report->fan_type[i]);
		drvdata->fan_type[i] = report->fan_type[i];
	}

	switch (report->type) {
	case FAN_STATUS_REPORT_SPEED:
		for (i = 0; i < FAN_CHANNELS; i++) {
			drvdata->fan_rpm[i] =
				get_unaligned_le16(&report->fan_speed.fan_rpm[i]);
			drvdata->fan_duty_percent[i] =
				report->fan_speed.duty_percent[i];
		}

		drvdata->pwm_status_received = true;
		wake_up_all_locked(&drvdata->wq);
		break;

	case FAN_STATUS_REPORT_VOLTAGE:
		for (i = 0; i < FAN_CHANNELS; i++) {
			drvdata->fan_in[i] =
				get_unaligned_le16(&report->fan_voltage.fan_in[i]);
			drvdata->fan_curr[i] =
				get_unaligned_le16(&report->fan_voltage.fan_current[i]);
		}

		drvdata->voltage_status_received = true;
		wake_up_all_locked(&drvdata->wq);
		break;
	}

	spin_unlock(&drvdata->wq.lock);
}

static umode_t nzxt_smart2_hwmon_is_visible(const void *data,
					    enum hwmon_sensor_types type,
					    u32 attr, int channel)
{
	switch (type) {
	case hwmon_pwm:
		switch (attr) {
		case hwmon_pwm_input:
		case hwmon_pwm_enable:
			return 0644;

		default:
			return 0444;
		}

	case hwmon_chip:
		switch (attr) {
		case hwmon_chip_update_interval:
			return 0644;

		default:
			return 0444;
		}

	default:
		return 0444;
	}
}

static int nzxt_smart2_hwmon_read(struct device *dev, enum hwmon_sensor_types type,
				  u32 attr, int channel, long *val)
{
	struct drvdata *drvdata = dev_get_drvdata(dev);
	int res = -EINVAL;

	if (type == hwmon_chip) {
		switch (attr) {
		case hwmon_chip_update_interval:
			*val = drvdata->update_interval;
			return 0;

		default:
			return -EINVAL;
		}
	}

	spin_lock_irq(&drvdata->wq.lock);

	switch (type) {
	case hwmon_pwm:
		/*
		 * fancontrol:
		 * 1) remembers pwm* values when it starts
		 * 2) needs pwm*_enable to be 1 on controlled fans
		 * So make sure we have correct data before allowing pwm* reads.
		 * Returning errors for pwm of fan speed read can even cause
		 * fancontrol to shut down. So the wait is unavoidable.
		 */
		switch (attr) {
		case hwmon_pwm_enable:
			res = wait_event_interruptible_locked_irq(drvdata->wq,
								  drvdata->fan_config_received);
			if (res)
				goto unlock;

			*val = drvdata->fan_type[channel] != FAN_TYPE_NONE;
			break;

		case hwmon_pwm_mode:
			res = wait_event_interruptible_locked_irq(drvdata->wq,
								  drvdata->fan_config_received);
			if (res)
				goto unlock;

			*val = drvdata->fan_type[channel] == FAN_TYPE_PWM;
			break;

		case hwmon_pwm_input:
			res = wait_event_interruptible_locked_irq(drvdata->wq,
								  drvdata->pwm_status_received);
			if (res)
				goto unlock;

			*val = scale_pwm_value(drvdata->fan_duty_percent[channel],
					       100, 255);
			break;
		}
		break;

	case hwmon_fan:
		/*
		 * It's not strictly necessary to wait for *_received in the
		 * remaining cases (fancontrol doesn't care about them). But I'm
		 * doing it to have consistent behavior.
		 */
		if (attr == hwmon_fan_input) {
			res = wait_event_interruptible_locked_irq(drvdata->wq,
								  drvdata->pwm_status_received);
			if (res)
				goto unlock;

			*val = drvdata->fan_rpm[channel];
		}
		break;

	case hwmon_in:
		if (attr == hwmon_in_input) {
			res = wait_event_interruptible_locked_irq(drvdata->wq,
								  drvdata->voltage_status_received);
			if (res)
				goto unlock;

			*val = drvdata->fan_in[channel];
		}
		break;

	case hwmon_curr:
		if (attr == hwmon_curr_input) {
			res = wait_event_interruptible_locked_irq(drvdata->wq,
								  drvdata->voltage_status_received);
			if (res)
				goto unlock;

			*val = drvdata->fan_curr[channel];
		}
		break;

	default:
		break;
	}

unlock:
	spin_unlock_irq(&drvdata->wq.lock);
	return res;
}

static int send_output_report(struct drvdata *drvdata, const void *data,
			      size_t data_size)
{
	int ret;

	if (data_size > sizeof(drvdata->output_buffer))
		return -EINVAL;

	memcpy(drvdata->output_buffer, data, data_size);

	if (data_size < sizeof(drvdata->output_buffer))
		memset(drvdata->output_buffer + data_size, 0,
		       sizeof(drvdata->output_buffer) - data_size);

	ret = hid_hw_output_report(drvdata->hid, drvdata->output_buffer,
				   sizeof(drvdata->output_buffer));
	return ret < 0 ? ret : 0;
}

static int set_pwm(struct drvdata *drvdata, int channel, long val)
{
	int ret;
	u8 duty_percent = scale_pwm_value(val, 255, 100);

	struct set_fan_speed_report report = {
		.report_id = OUTPUT_REPORT_ID_SET_FAN_SPEED,
		.magic = 1,
		.channel_bit_mask = 1 << channel
	};

	ret = mutex_lock_interruptible(&drvdata->mutex);
	if (ret)
		return ret;

	report.duty_percent[channel] = duty_percent;
	ret = send_output_report(drvdata, &report, sizeof(report));
	if (ret)
		goto unlock;

	/*
	 * pwmconfig and fancontrol scripts expect pwm writes to take effect
	 * immediately (i. e. read from pwm* sysfs should return the value
	 * written into it). The device seems to always accept pwm values - even
	 * when there is no fan connected - so update pwm status without waiting
	 * for a report, to make pwmconfig and fancontrol happy. Worst case -
	 * if the device didn't accept new pwm value for some reason (never seen
	 * this in practice) - it will be reported incorrectly only until next
	 * update. This avoids "fan stuck" messages from pwmconfig, and
	 * fancontrol setting fan speed to 100% during shutdown.
	 */
	spin_lock_bh(&drvdata->wq.lock);
	drvdata->fan_duty_percent[channel] = duty_percent;
	spin_unlock_bh(&drvdata->wq.lock);

unlock:
	mutex_unlock(&drvdata->mutex);
	return ret;
}

/*
 * Workaround for fancontrol/pwmconfig trying to write to pwm*_enable even if it
 * already is 1 and read-only. Otherwise, fancontrol won't restore pwm on
 * shutdown properly.
 */
static int set_pwm_enable(struct drvdata *drvdata, int channel, long val)
{
	long expected_val;
	int res;

	spin_lock_irq(&drvdata->wq.lock);

	res = wait_event_interruptible_locked_irq(drvdata->wq,
						  drvdata->fan_config_received);
	if (res) {
		spin_unlock_irq(&drvdata->wq.lock);
		return res;
	}

	expected_val = drvdata->fan_type[channel] != FAN_TYPE_NONE;

	spin_unlock_irq(&drvdata->wq.lock);

	return (val == expected_val) ? 0 : -EOPNOTSUPP;
}

/*
 * Control byte	| Actual update interval in seconds
 * 0xff		| 65.5
 * 0xf7		| 63.46
 * 0x7f		| 32.74
 * 0x3f		| 16.36
 * 0x1f		| 8.17
 * 0x0f		| 4.07
 * 0x07		| 2.02
 * 0x03		| 1.00
 * 0x02		| 0.744
 * 0x01		| 0.488
 * 0x00		| 0.25
 */
static u8 update_interval_to_control_byte(long interval)
{
	if (interval <= 250)
		return 0;

	return clamp_val(1 + DIV_ROUND_CLOSEST(interval - 488, 256), 0, 255);
}

static long control_byte_to_update_interval(u8 control_byte)
{
	if (control_byte == 0)
		return 250;

	return 488 + (control_byte - 1) * 256;
}

static int set_update_interval(struct drvdata *drvdata, long val)
{
	u8 control = update_interval_to_control_byte(val);
	u8 report[] = {
		OUTPUT_REPORT_ID_INIT_COMMAND,
		INIT_COMMAND_SET_UPDATE_INTERVAL,
		0x01,
		0xe8,
		control,
		0x01,
		0xe8,
		control,
	};
	int ret;

	ret = send_output_report(drvdata, report, sizeof(report));
	if (ret)
		return ret;

	drvdata->update_interval = control_byte_to_update_interval(control);
	return 0;
}

static int init_device(struct drvdata *drvdata, long update_interval)
{
	int ret;
	static const u8 detect_fans_report[] = {
		OUTPUT_REPORT_ID_INIT_COMMAND,
		INIT_COMMAND_DETECT_FANS,
	};

	ret = send_output_report(drvdata, detect_fans_report,
				 sizeof(detect_fans_report));
	if (ret)
		return ret;

	return set_update_interval(drvdata, update_interval);
}

static int nzxt_smart2_hwmon_write(struct device *dev,
				   enum hwmon_sensor_types type, u32 attr,
				   int channel, long val)
{
	struct drvdata *drvdata = dev_get_drvdata(dev);
	int ret;

	switch (type) {
	case hwmon_pwm:
		switch (attr) {
		case hwmon_pwm_enable:
			return set_pwm_enable(drvdata, channel, val);

		case hwmon_pwm_input:
			return set_pwm(drvdata, channel, val);

		default:
			return -EINVAL;
		}

	case hwmon_chip:
		switch (attr) {
		case hwmon_chip_update_interval:
			ret = mutex_lock_interruptible(&drvdata->mutex);
			if (ret)
				return ret;

			ret = set_update_interval(drvdata, val);

			mutex_unlock(&drvdata->mutex);
			return ret;

		default:
			return -EINVAL;
		}

	default:
		return -EINVAL;
	}
}

static int nzxt_smart2_hwmon_read_string(struct device *dev,
					 enum hwmon_sensor_types type, u32 attr,
					 int channel, const char **str)
{
	switch (type) {
	case hwmon_fan:
		*str = fan_label[channel];
		return 0;
	case hwmon_curr:
		*str = curr_label[channel];
		return 0;
	case hwmon_in:
		*str = in_label[channel];
		return 0;
	default:
		return -EINVAL;
	}
}

static const struct hwmon_ops nzxt_smart2_hwmon_ops = {
	.is_visible = nzxt_smart2_hwmon_is_visible,
	.read = nzxt_smart2_hwmon_read,
	.read_string = nzxt_smart2_hwmon_read_string,
	.write = nzxt_smart2_hwmon_write,
};

static const struct hwmon_channel_info * const nzxt_smart2_channel_info[] = {
	HWMON_CHANNEL_INFO(fan, HWMON_F_INPUT | HWMON_F_LABEL,
			   HWMON_F_INPUT | HWMON_F_LABEL,
			   HWMON_F_INPUT | HWMON_F_LABEL),
	HWMON_CHANNEL_INFO(pwm, HWMON_PWM_INPUT | HWMON_PWM_MODE | HWMON_PWM_ENABLE,
			   HWMON_PWM_INPUT | HWMON_PWM_MODE | HWMON_PWM_ENABLE,
			   HWMON_PWM_INPUT | HWMON_PWM_MODE | HWMON_PWM_ENABLE),
	HWMON_CHANNEL_INFO(in, HWMON_I_INPUT | HWMON_I_LABEL,
			   HWMON_I_INPUT | HWMON_I_LABEL,
			   HWMON_I_INPUT | HWMON_I_LABEL),
	HWMON_CHANNEL_INFO(curr, HWMON_C_INPUT | HWMON_C_LABEL,
			   HWMON_C_INPUT | HWMON_C_LABEL,
			   HWMON_C_INPUT | HWMON_C_LABEL),
	HWMON_CHANNEL_INFO(chip, HWMON_C_UPDATE_INTERVAL),
	NULL
};

static const struct hwmon_chip_info nzxt_smart2_chip_info = {
	.ops = &nzxt_smart2_hwmon_ops,
	.info = nzxt_smart2_channel_info,
};

static int nzxt_smart2_hid_raw_event(struct hid_device *hdev,
				     struct hid_report *report, u8 *data, int size)
{
	struct drvdata *drvdata = hid_get_drvdata(hdev);
	u8 report_id = *data;

	switch (report_id) {
	case INPUT_REPORT_ID_FAN_CONFIG:
		handle_fan_config_report(drvdata, data, size);
		break;

	case INPUT_REPORT_ID_FAN_STATUS:
		handle_fan_status_report(drvdata, data, size);
		break;
	}

	return 0;
}

static int __maybe_unused nzxt_smart2_hid_reset_resume(struct hid_device *hdev)
{
	struct drvdata *drvdata = hid_get_drvdata(hdev);

	/*
	 * Userspace is still frozen (so no concurrent sysfs attribute access
	 * is possible), but raw_event can already be called concurrently.
	 */
	spin_lock_bh(&drvdata->wq.lock);
	drvdata->fan_config_received = false;
	drvdata->pwm_status_received = false;
	drvdata->voltage_status_received = false;
	spin_unlock_bh(&drvdata->wq.lock);

	return init_device(drvdata, drvdata->update_interval);
}

static void mutex_fini(void *lock)
{
	mutex_destroy(lock);
}

static int nzxt_smart2_hid_probe(struct hid_device *hdev,
				 const struct hid_device_id *id)
{
	struct drvdata *drvdata;
	int ret;

	drvdata = devm_kzalloc(&hdev->dev, sizeof(struct drvdata), GFP_KERNEL);
	if (!drvdata)
		return -ENOMEM;

	drvdata->hid = hdev;
	hid_set_drvdata(hdev, drvdata);

	init_waitqueue_head(&drvdata->wq);

	mutex_init(&drvdata->mutex);
	ret = devm_add_action_or_reset(&hdev->dev, mutex_fini, &drvdata->mutex);
	if (ret)
		return ret;

	ret = hid_parse(hdev);
	if (ret)
		return ret;

	ret = hid_hw_start(hdev, HID_CONNECT_HIDRAW);
	if (ret)
		return ret;

	ret = hid_hw_open(hdev);
	if (ret)
		goto out_hw_stop;

	hid_device_io_start(hdev);

	init_device(drvdata, UPDATE_INTERVAL_DEFAULT_MS);

	drvdata->hwmon =
		hwmon_device_register_with_info(&hdev->dev, "nzxtsmart2", drvdata,
						&nzxt_smart2_chip_info, NULL);
	if (IS_ERR(drvdata->hwmon)) {
		ret = PTR_ERR(drvdata->hwmon);
		goto out_hw_close;
	}

	return 0;

out_hw_close:
	hid_hw_close(hdev);

out_hw_stop:
	hid_hw_stop(hdev);
	return ret;
}

static void nzxt_smart2_hid_remove(struct hid_device *hdev)
{
	struct drvdata *drvdata = hid_get_drvdata(hdev);

	hwmon_device_unregister(drvdata->hwmon);

	hid_hw_close(hdev);
	hid_hw_stop(hdev);
}

static const struct hid_device_id nzxt_smart2_hid_id_table[] = {
	{ HID_USB_DEVICE(0x1e71, 0x2006) }, /* NZXT Smart Device V2 */
	{ HID_USB_DEVICE(0x1e71, 0x200d) }, /* NZXT Smart Device V2 */
	{ HID_USB_DEVICE(0x1e71, 0x200f) }, /* NZXT Smart Device V2 */
	{ HID_USB_DEVICE(0x1e71, 0x2009) }, /* NZXT RGB & Fan Controller */
	{ HID_USB_DEVICE(0x1e71, 0x200e) }, /* NZXT RGB & Fan Controller */
	{ HID_USB_DEVICE(0x1e71, 0x2010) }, /* NZXT RGB & Fan Controller */
	{ HID_USB_DEVICE(0x1e71, 0x2011) }, /* NZXT RGB & Fan Controller (6 RGB) */
	{ HID_USB_DEVICE(0x1e71, 0x2019) }, /* NZXT RGB & Fan Controller (6 RGB) */
	{ HID_USB_DEVICE(0x1e71, 0x2020) }, /* NZXT RGB & Fan Controller (6 RGB) */
	{},
};

static struct hid_driver nzxt_smart2_hid_driver = {
	.name = "nzxt-smart2",
	.id_table = nzxt_smart2_hid_id_table,
	.probe = nzxt_smart2_hid_probe,
	.remove = nzxt_smart2_hid_remove,
	.raw_event = nzxt_smart2_hid_raw_event,
#ifdef CONFIG_PM
	.reset_resume = nzxt_smart2_hid_reset_resume,
#endif
};

static int __init nzxt_smart2_init(void)
{
	return hid_register_driver(&nzxt_smart2_hid_driver);
}

static void __exit nzxt_smart2_exit(void)
{
	hid_unregister_driver(&nzxt_smart2_hid_driver);
}

MODULE_DEVICE_TABLE(hid, nzxt_smart2_hid_id_table);
MODULE_AUTHOR("Aleksandr Mezin <mezin.alexander@gmail.com>");
MODULE_DESCRIPTION("Driver for NZXT RGB & Fan Controller/Smart Device V2");
MODULE_LICENSE("GPL");

/*
 * With module_init()/module_hid_driver() and the driver built into the kernel:
 *
 * Driver 'nzxt_smart2' was unable to register with bus_type 'hid' because the
 * bus was not initialized.
 */
late_initcall(nzxt_smart2_init);
module_exit(nzxt_smart2_exit);
