// SPDX-License-Identifier: GPL-2.0
/*
 * CZ.NIC's Turris Omnia MCU system off and RTC wakeup driver
 *
 * This is not a true RTC driver (in the sense that it does not provide a
 * real-time clock), rather the MCU implements a wakeup from powered off state
 * at a specified time relative to MCU boot, and we expose this feature via RTC
 * alarm, so that it can be used via the rtcwake command, which is the standard
 * Linux command for this.
 *
 * 2024 by Marek Behún <kabel@kernel.org>
 */

#include <linux/crc32.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/kstrtox.h>
#include <linux/reboot.h>
#include <linux/rtc.h>
#include <linux/sysfs.h>
#include <linux/types.h>

#include <linux/turris-omnia-mcu-interface.h>
#include "turris-omnia-mcu.h"

static int omnia_get_uptime_wakeup(const struct i2c_client *client, u32 *uptime,
				   u32 *wakeup)
{
	__le32 reply[2];
	int err;

	err = omnia_cmd_read(client, OMNIA_CMD_GET_UPTIME_AND_WAKEUP, reply,
			     sizeof(reply));
	if (err)
		return err;

	if (uptime)
		*uptime = le32_to_cpu(reply[0]);

	if (wakeup)
		*wakeup = le32_to_cpu(reply[1]);

	return 0;
}

static int omnia_read_time(struct device *dev, struct rtc_time *tm)
{
	u32 uptime;
	int err;

	err = omnia_get_uptime_wakeup(to_i2c_client(dev), &uptime, NULL);
	if (err)
		return err;

	rtc_time64_to_tm(uptime, tm);

	return 0;
}

static int omnia_read_alarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct omnia_mcu *mcu = i2c_get_clientdata(client);
	u32 wakeup;
	int err;

	err = omnia_get_uptime_wakeup(client, NULL, &wakeup);
	if (err)
		return err;

	alrm->enabled = !!wakeup;
	rtc_time64_to_tm(wakeup ?: mcu->rtc_alarm, &alrm->time);

	return 0;
}

static int omnia_set_alarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct omnia_mcu *mcu = i2c_get_clientdata(client);

	mcu->rtc_alarm = rtc_tm_to_time64(&alrm->time);

	if (alrm->enabled)
		return omnia_cmd_write_u32(client, OMNIA_CMD_SET_WAKEUP,
					   mcu->rtc_alarm);

	return 0;
}

static int omnia_alarm_irq_enable(struct device *dev, unsigned int enabled)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct omnia_mcu *mcu = i2c_get_clientdata(client);

	return omnia_cmd_write_u32(client, OMNIA_CMD_SET_WAKEUP,
				   enabled ? mcu->rtc_alarm : 0);
}

static const struct rtc_class_ops omnia_rtc_ops = {
	.read_time		= omnia_read_time,
	.read_alarm		= omnia_read_alarm,
	.set_alarm		= omnia_set_alarm,
	.alarm_irq_enable	= omnia_alarm_irq_enable,
};

static int omnia_power_off(struct sys_off_data *data)
{
	struct omnia_mcu *mcu = data->cb_data;
	__be32 tmp;
	u8 cmd[9];
	u16 arg;
	int err;

	if (mcu->front_button_poweron)
		arg = OMNIA_CMD_POWER_OFF_POWERON_BUTTON;
	else
		arg = 0;

	cmd[0] = OMNIA_CMD_POWER_OFF;
	put_unaligned_le16(OMNIA_CMD_POWER_OFF_MAGIC, &cmd[1]);
	put_unaligned_le16(arg, &cmd[3]);

	/*
	 * Although all values from and to MCU are passed in little-endian, the
	 * MCU's CRC unit uses big-endian CRC32 polynomial (0x04c11db7), so we
	 * need to use crc32_be() here.
	 */
	tmp = cpu_to_be32(get_unaligned_le32(&cmd[1]));
	put_unaligned_le32(crc32_be(~0, (void *)&tmp, sizeof(tmp)), &cmd[5]);

	err = omnia_cmd_write(mcu->client, cmd, sizeof(cmd));
	if (err)
		dev_err(&mcu->client->dev,
			"Unable to send the poweroff command: %d\n", err);

	return NOTIFY_DONE;
}

static int omnia_restart(struct sys_off_data *data)
{
	struct omnia_mcu *mcu = data->cb_data;
	u8 cmd[3];
	int err;

	cmd[0] = OMNIA_CMD_GENERAL_CONTROL;

	if (reboot_mode == REBOOT_HARD)
		cmd[1] = cmd[2] = OMNIA_CTL_HARD_RST;
	else
		cmd[1] = cmd[2] = OMNIA_CTL_LIGHT_RST;

	err = omnia_cmd_write(mcu->client, cmd, sizeof(cmd));
	if (err)
		dev_err(&mcu->client->dev,
			"Unable to send the restart command: %d\n", err);

	/*
	 * MCU needs a little bit to process the I2C command, otherwise it will
	 * do a light reset based on SOC SYSRES_OUT pin.
	 */
	mdelay(1);

	return NOTIFY_DONE;
}

static ssize_t front_button_poweron_show(struct device *dev,
					 struct device_attribute *a, char *buf)
{
	struct omnia_mcu *mcu = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%d\n", mcu->front_button_poweron);
}

static ssize_t front_button_poweron_store(struct device *dev,
					  struct device_attribute *a,
					  const char *buf, size_t count)
{
	struct omnia_mcu *mcu = dev_get_drvdata(dev);
	bool val;
	int err;

	err = kstrtobool(buf, &val);
	if (err)
		return err;

	mcu->front_button_poweron = val;

	return count;
}
static DEVICE_ATTR_RW(front_button_poweron);

static struct attribute *omnia_mcu_poweroff_attrs[] = {
	&dev_attr_front_button_poweron.attr,
	NULL
};

static umode_t poweroff_attrs_visible(struct kobject *kobj, struct attribute *a,
				      int n)
{
	struct device *dev = kobj_to_dev(kobj);
	struct omnia_mcu *mcu = dev_get_drvdata(dev);

	if (mcu->features & OMNIA_FEAT_POWEROFF_WAKEUP)
		return a->mode;

	return 0;
}

const struct attribute_group omnia_mcu_poweroff_group = {
	.attrs = omnia_mcu_poweroff_attrs,
	.is_visible = poweroff_attrs_visible,
};

int omnia_mcu_register_sys_off_and_wakeup(struct omnia_mcu *mcu)
{
	struct device *dev = &mcu->client->dev;
	int err;

	/* MCU restart is always available */
	err = devm_register_sys_off_handler(dev, SYS_OFF_MODE_RESTART,
					    SYS_OFF_PRIO_FIRMWARE,
					    omnia_restart, mcu);
	if (err)
		return dev_err_probe(dev, err,
				     "Cannot register system restart handler\n");

	/*
	 * Poweroff and wakeup are available only if POWEROFF_WAKEUP feature is
	 * present.
	 */
	if (!(mcu->features & OMNIA_FEAT_POWEROFF_WAKEUP))
		return 0;

	err = devm_register_sys_off_handler(dev, SYS_OFF_MODE_POWER_OFF,
					    SYS_OFF_PRIO_FIRMWARE,
					    omnia_power_off, mcu);
	if (err)
		return dev_err_probe(dev, err,
				     "Cannot register system power off handler\n");

	mcu->rtcdev = devm_rtc_allocate_device(dev);
	if (IS_ERR(mcu->rtcdev))
		return dev_err_probe(dev, PTR_ERR(mcu->rtcdev),
				     "Cannot allocate RTC device\n");

	mcu->rtcdev->ops = &omnia_rtc_ops;
	mcu->rtcdev->range_max = U32_MAX;
	set_bit(RTC_FEATURE_ALARM_WAKEUP_ONLY, mcu->rtcdev->features);

	err = devm_rtc_register_device(mcu->rtcdev);
	if (err)
		return dev_err_probe(dev, err, "Cannot register RTC device\n");

	mcu->front_button_poweron = true;

	return 0;
}
