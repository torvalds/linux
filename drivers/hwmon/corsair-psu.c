// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * corsair-psu.c - Linux driver for Corsair power supplies with HID sensors interface
 * Copyright (C) 2020 Wilken Gottwalt <wilken.gottwalt@posteo.net>
 */

#include <linux/completion.h>
#include <linux/debugfs.h>
#include <linux/errno.h>
#include <linux/hid.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/types.h>

/*
 * Corsair protocol for PSUs
 *
 * message size = 64 bytes (request and response, little endian)
 * request:
 *	[length][command][param0][param1][paramX]...
 * reply:
 *	[echo of length][echo of command][data0][data1][dataX]...
 *
 *	- commands are byte sized opcodes
 *	- length is the sum of all bytes of the commands/params
 *	- the micro-controller of most of these PSUs support concatenation in the request and reply,
 *	  but it is better to not rely on this (it is also hard to parse)
 *	- the driver uses raw events to be accessible from userspace (though this is not really
 *	  supported, it is just there for convenience, may be removed in the future)
 *	- a reply always start with the length and command in the same order the request used it
 *	- length of the reply data is specific to the command used
 *	- some of the commands work on a rail and can be switched to a specific rail (0 = 12v,
 *	  1 = 5v, 2 = 3.3v)
 *	- the format of the init command 0xFE is swapped length/command bytes
 *	- parameter bytes amount and values are specific to the command (rail setting is the only
 *	  for now that uses non-zero values)
 *	- there are much more commands, especially for configuring the device, but they are not
 *	  supported because a wrong command/length can lockup the micro-controller
 *	- the driver supports debugfs for values not fitting into the hwmon class
 *	- not every device class (HXi, RMi or AXi) supports all commands
 *	- it is a pure sensors reading driver (will not support configuring)
 */

#define DRIVER_NAME		"corsair-psu"

#define REPLY_SIZE		16 /* max length of a reply to a single command */
#define CMD_BUFFER_SIZE		64
#define CMD_TIMEOUT_MS		250
#define SECONDS_PER_HOUR	(60 * 60)
#define SECONDS_PER_DAY		(SECONDS_PER_HOUR * 24)
#define RAIL_COUNT		3 /* 3v3 + 5v + 12v */
#define TEMP_COUNT		2

#define PSU_CMD_SELECT_RAIL	0x00 /* expects length 2 */
#define PSU_CMD_RAIL_VOLTS_HCRIT 0x40 /* the rest of the commands expect length 3 */
#define PSU_CMD_RAIL_VOLTS_LCRIT 0x44
#define PSU_CMD_RAIL_AMPS_HCRIT	0x46
#define PSU_CMD_TEMP_HCRIT	0x4F
#define PSU_CMD_IN_VOLTS	0x88
#define PSU_CMD_IN_AMPS		0x89
#define PSU_CMD_RAIL_VOLTS	0x8B
#define PSU_CMD_RAIL_AMPS	0x8C
#define PSU_CMD_TEMP0		0x8D
#define PSU_CMD_TEMP1		0x8E
#define PSU_CMD_FAN		0x90
#define PSU_CMD_RAIL_WATTS	0x96
#define PSU_CMD_VEND_STR	0x99
#define PSU_CMD_PROD_STR	0x9A
#define PSU_CMD_TOTAL_WATTS	0xEE
#define PSU_CMD_TOTAL_UPTIME	0xD1
#define PSU_CMD_UPTIME		0xD2
#define PSU_CMD_INIT		0xFE

#define L_IN_VOLTS		"v_in"
#define L_OUT_VOLTS_12V		"v_out +12v"
#define L_OUT_VOLTS_5V		"v_out +5v"
#define L_OUT_VOLTS_3_3V	"v_out +3.3v"
#define L_IN_AMPS		"curr in"
#define L_AMPS_12V		"curr +12v"
#define L_AMPS_5V		"curr +5v"
#define L_AMPS_3_3V		"curr +3.3v"
#define L_FAN			"psu fan"
#define L_TEMP0			"vrm temp"
#define L_TEMP1			"case temp"
#define L_WATTS			"power total"
#define L_WATTS_12V		"power +12v"
#define L_WATTS_5V		"power +5v"
#define L_WATTS_3_3V		"power +3.3v"

static const char *const label_watts[] = {
	L_WATTS,
	L_WATTS_12V,
	L_WATTS_5V,
	L_WATTS_3_3V
};

static const char *const label_volts[] = {
	L_IN_VOLTS,
	L_OUT_VOLTS_12V,
	L_OUT_VOLTS_5V,
	L_OUT_VOLTS_3_3V
};

static const char *const label_amps[] = {
	L_IN_AMPS,
	L_AMPS_12V,
	L_AMPS_5V,
	L_AMPS_3_3V
};

struct corsairpsu_data {
	struct hid_device *hdev;
	struct device *hwmon_dev;
	struct dentry *debugfs;
	struct completion wait_completion;
	struct mutex lock; /* for locking access to cmd_buffer */
	u8 *cmd_buffer;
	char vendor[REPLY_SIZE];
	char product[REPLY_SIZE];
	long temp_crit[TEMP_COUNT];
	long in_crit[RAIL_COUNT];
	long in_lcrit[RAIL_COUNT];
	long curr_crit[RAIL_COUNT];
	u8 temp_crit_support;
	u8 in_crit_support;
	u8 in_lcrit_support;
	u8 curr_crit_support;
	bool in_curr_cmd_support; /* not all commands are supported on every PSU */
};

/* some values are SMBus LINEAR11 data which need a conversion */
static int corsairpsu_linear11_to_int(const u16 val, const int scale)
{
	const int exp = ((s16)val) >> 11;
	const int mant = (((s16)(val & 0x7ff)) << 5) >> 5;
	const int result = mant * scale;

	return (exp >= 0) ? (result << exp) : (result >> -exp);
}

static int corsairpsu_usb_cmd(struct corsairpsu_data *priv, u8 p0, u8 p1, u8 p2, void *data)
{
	unsigned long time;
	int ret;

	memset(priv->cmd_buffer, 0, CMD_BUFFER_SIZE);
	priv->cmd_buffer[0] = p0;
	priv->cmd_buffer[1] = p1;
	priv->cmd_buffer[2] = p2;

	reinit_completion(&priv->wait_completion);

	ret = hid_hw_output_report(priv->hdev, priv->cmd_buffer, CMD_BUFFER_SIZE);
	if (ret < 0)
		return ret;

	time = wait_for_completion_timeout(&priv->wait_completion,
					   msecs_to_jiffies(CMD_TIMEOUT_MS));
	if (!time)
		return -ETIMEDOUT;

	/*
	 * at the start of the reply is an echo of the send command/length in the same order it
	 * was send, not every command is supported on every device class, if a command is not
	 * supported, the length value in the reply is okay, but the command value is set to 0
	 */
	if (p0 != priv->cmd_buffer[0] || p1 != priv->cmd_buffer[1])
		return -EOPNOTSUPP;

	if (data)
		memcpy(data, priv->cmd_buffer + 2, REPLY_SIZE);

	return 0;
}

static int corsairpsu_init(struct corsairpsu_data *priv)
{
	/*
	 * PSU_CMD_INIT uses swapped length/command and expects 2 parameter bytes, this command
	 * actually generates a reply, but we don't need it
	 */
	return corsairpsu_usb_cmd(priv, PSU_CMD_INIT, 3, 0, NULL);
}

static int corsairpsu_fwinfo(struct corsairpsu_data *priv)
{
	int ret;

	ret = corsairpsu_usb_cmd(priv, 3, PSU_CMD_VEND_STR, 0, priv->vendor);
	if (ret < 0)
		return ret;

	ret = corsairpsu_usb_cmd(priv, 3, PSU_CMD_PROD_STR, 0, priv->product);
	if (ret < 0)
		return ret;

	return 0;
}

static int corsairpsu_request(struct corsairpsu_data *priv, u8 cmd, u8 rail, void *data)
{
	int ret;

	mutex_lock(&priv->lock);
	switch (cmd) {
	case PSU_CMD_RAIL_VOLTS_HCRIT:
	case PSU_CMD_RAIL_VOLTS_LCRIT:
	case PSU_CMD_RAIL_AMPS_HCRIT:
	case PSU_CMD_RAIL_VOLTS:
	case PSU_CMD_RAIL_AMPS:
	case PSU_CMD_RAIL_WATTS:
		ret = corsairpsu_usb_cmd(priv, 2, PSU_CMD_SELECT_RAIL, rail, NULL);
		if (ret < 0)
			goto cmd_fail;
		break;
	default:
		break;
	}

	ret = corsairpsu_usb_cmd(priv, 3, cmd, 0, data);

cmd_fail:
	mutex_unlock(&priv->lock);
	return ret;
}

static int corsairpsu_get_value(struct corsairpsu_data *priv, u8 cmd, u8 rail, long *val)
{
	u8 data[REPLY_SIZE];
	long tmp;
	int ret;

	ret = corsairpsu_request(priv, cmd, rail, data);
	if (ret < 0)
		return ret;

	/*
	 * the biggest value here comes from the uptime command and to exceed MAXINT total uptime
	 * needs to be about 68 years, the rest are u16 values and the biggest value coming out of
	 * the LINEAR11 conversion are the watts values which are about 1200 for the strongest psu
	 * supported (HX1200i)
	 */
	tmp = ((long)data[3] << 24) + (data[2] << 16) + (data[1] << 8) + data[0];
	switch (cmd) {
	case PSU_CMD_RAIL_VOLTS_HCRIT:
	case PSU_CMD_RAIL_VOLTS_LCRIT:
	case PSU_CMD_RAIL_AMPS_HCRIT:
	case PSU_CMD_TEMP_HCRIT:
	case PSU_CMD_IN_VOLTS:
	case PSU_CMD_IN_AMPS:
	case PSU_CMD_RAIL_VOLTS:
	case PSU_CMD_RAIL_AMPS:
	case PSU_CMD_TEMP0:
	case PSU_CMD_TEMP1:
		*val = corsairpsu_linear11_to_int(tmp & 0xFFFF, 1000);
		break;
	case PSU_CMD_FAN:
		*val = corsairpsu_linear11_to_int(tmp & 0xFFFF, 1);
		break;
	case PSU_CMD_RAIL_WATTS:
	case PSU_CMD_TOTAL_WATTS:
		*val = corsairpsu_linear11_to_int(tmp & 0xFFFF, 1000000);
		break;
	case PSU_CMD_TOTAL_UPTIME:
	case PSU_CMD_UPTIME:
		*val = tmp;
		break;
	default:
		ret = -EOPNOTSUPP;
		break;
	}

	return ret;
}

static void corsairpsu_get_criticals(struct corsairpsu_data *priv)
{
	long tmp;
	int rail;

	for (rail = 0; rail < TEMP_COUNT; ++rail) {
		if (!corsairpsu_get_value(priv, PSU_CMD_TEMP_HCRIT, rail, &tmp)) {
			priv->temp_crit_support |= BIT(rail);
			priv->temp_crit[rail] = tmp;
		}
	}

	for (rail = 0; rail < RAIL_COUNT; ++rail) {
		if (!corsairpsu_get_value(priv, PSU_CMD_RAIL_VOLTS_HCRIT, rail, &tmp)) {
			priv->in_crit_support |= BIT(rail);
			priv->in_crit[rail] = tmp;
		}

		if (!corsairpsu_get_value(priv, PSU_CMD_RAIL_VOLTS_LCRIT, rail, &tmp)) {
			priv->in_lcrit_support |= BIT(rail);
			priv->in_lcrit[rail] = tmp;
		}

		if (!corsairpsu_get_value(priv, PSU_CMD_RAIL_AMPS_HCRIT, rail, &tmp)) {
			priv->curr_crit_support |= BIT(rail);
			priv->curr_crit[rail] = tmp;
		}
	}
}

static void corsairpsu_check_cmd_support(struct corsairpsu_data *priv)
{
	long tmp;

	priv->in_curr_cmd_support = !corsairpsu_get_value(priv, PSU_CMD_IN_AMPS, 0, &tmp);
}

static umode_t corsairpsu_hwmon_temp_is_visible(const struct corsairpsu_data *priv, u32 attr,
						int channel)
{
	umode_t res = 0444;

	switch (attr) {
	case hwmon_temp_input:
	case hwmon_temp_label:
	case hwmon_temp_crit:
		if (channel > 0 && !(priv->temp_crit_support & BIT(channel - 1)))
			res = 0;
		break;
	default:
		break;
	}

	return res;
}

static umode_t corsairpsu_hwmon_fan_is_visible(const struct corsairpsu_data *priv, u32 attr,
					       int channel)
{
	switch (attr) {
	case hwmon_fan_input:
	case hwmon_fan_label:
		return 0444;
	default:
		return 0;
	}
}

static umode_t corsairpsu_hwmon_power_is_visible(const struct corsairpsu_data *priv, u32 attr,
						 int channel)
{
	switch (attr) {
	case hwmon_power_input:
	case hwmon_power_label:
		return 0444;
	default:
		return 0;
	}
}

static umode_t corsairpsu_hwmon_in_is_visible(const struct corsairpsu_data *priv, u32 attr,
					      int channel)
{
	umode_t res = 0444;

	switch (attr) {
	case hwmon_in_input:
	case hwmon_in_label:
	case hwmon_in_crit:
		if (channel > 0 && !(priv->in_crit_support & BIT(channel - 1)))
			res = 0;
		break;
	case hwmon_in_lcrit:
		if (channel > 0 && !(priv->in_lcrit_support & BIT(channel - 1)))
			res = 0;
		break;
	default:
		break;
	}

	return res;
}

static umode_t corsairpsu_hwmon_curr_is_visible(const struct corsairpsu_data *priv, u32 attr,
						int channel)
{
	umode_t res = 0444;

	switch (attr) {
	case hwmon_curr_input:
		if (channel == 0 && !priv->in_curr_cmd_support)
			res = 0;
		break;
	case hwmon_curr_label:
	case hwmon_curr_crit:
		if (channel > 0 && !(priv->curr_crit_support & BIT(channel - 1)))
			res = 0;
		break;
	default:
		break;
	}

	return res;
}

static umode_t corsairpsu_hwmon_ops_is_visible(const void *data, enum hwmon_sensor_types type,
					       u32 attr, int channel)
{
	const struct corsairpsu_data *priv = data;

	switch (type) {
	case hwmon_temp:
		return corsairpsu_hwmon_temp_is_visible(priv, attr, channel);
	case hwmon_fan:
		return corsairpsu_hwmon_fan_is_visible(priv, attr, channel);
	case hwmon_power:
		return corsairpsu_hwmon_power_is_visible(priv, attr, channel);
	case hwmon_in:
		return corsairpsu_hwmon_in_is_visible(priv, attr, channel);
	case hwmon_curr:
		return corsairpsu_hwmon_curr_is_visible(priv, attr, channel);
	default:
		return 0;
	}
}

static int corsairpsu_hwmon_temp_read(struct corsairpsu_data *priv, u32 attr, int channel,
				      long *val)
{
	int err = -EOPNOTSUPP;

	switch (attr) {
	case hwmon_temp_input:
		return corsairpsu_get_value(priv, channel ? PSU_CMD_TEMP1 : PSU_CMD_TEMP0,
					    channel, val);
	case hwmon_temp_crit:
		*val = priv->temp_crit[channel];
		err = 0;
		break;
	default:
		break;
	}

	return err;
}

static int corsairpsu_hwmon_power_read(struct corsairpsu_data *priv, u32 attr, int channel,
				       long *val)
{
	if (attr == hwmon_power_input) {
		switch (channel) {
		case 0:
			return corsairpsu_get_value(priv, PSU_CMD_TOTAL_WATTS, 0, val);
		case 1 ... 3:
			return corsairpsu_get_value(priv, PSU_CMD_RAIL_WATTS, channel - 1, val);
		default:
			break;
		}
	}

	return -EOPNOTSUPP;
}

static int corsairpsu_hwmon_in_read(struct corsairpsu_data *priv, u32 attr, int channel, long *val)
{
	int err = -EOPNOTSUPP;

	switch (attr) {
	case hwmon_in_input:
		switch (channel) {
		case 0:
			return corsairpsu_get_value(priv, PSU_CMD_IN_VOLTS, 0, val);
		case 1 ... 3:
			return corsairpsu_get_value(priv, PSU_CMD_RAIL_VOLTS, channel - 1, val);
		default:
			break;
		}
		break;
	case hwmon_in_crit:
		*val = priv->in_crit[channel - 1];
		err = 0;
		break;
	case hwmon_in_lcrit:
		*val = priv->in_lcrit[channel - 1];
		err = 0;
		break;
	}

	return err;
}

static int corsairpsu_hwmon_curr_read(struct corsairpsu_data *priv, u32 attr, int channel,
				      long *val)
{
	int err = -EOPNOTSUPP;

	switch (attr) {
	case hwmon_curr_input:
		switch (channel) {
		case 0:
			return corsairpsu_get_value(priv, PSU_CMD_IN_AMPS, 0, val);
		case 1 ... 3:
			return corsairpsu_get_value(priv, PSU_CMD_RAIL_AMPS, channel - 1, val);
		default:
			break;
		}
		break;
	case hwmon_curr_crit:
		*val = priv->curr_crit[channel - 1];
		err = 0;
		break;
	default:
		break;
	}

	return err;
}

static int corsairpsu_hwmon_ops_read(struct device *dev, enum hwmon_sensor_types type, u32 attr,
				     int channel, long *val)
{
	struct corsairpsu_data *priv = dev_get_drvdata(dev);

	switch (type) {
	case hwmon_temp:
		return corsairpsu_hwmon_temp_read(priv, attr, channel, val);
	case hwmon_fan:
		if (attr == hwmon_fan_input)
			return corsairpsu_get_value(priv, PSU_CMD_FAN, 0, val);
		return -EOPNOTSUPP;
	case hwmon_power:
		return corsairpsu_hwmon_power_read(priv, attr, channel, val);
	case hwmon_in:
		return corsairpsu_hwmon_in_read(priv, attr, channel, val);
	case hwmon_curr:
		return corsairpsu_hwmon_curr_read(priv, attr, channel, val);
	default:
		return -EOPNOTSUPP;
	}
}

static int corsairpsu_hwmon_ops_read_string(struct device *dev, enum hwmon_sensor_types type,
					    u32 attr, int channel, const char **str)
{
	if (type == hwmon_temp && attr == hwmon_temp_label) {
		*str = channel ? L_TEMP1 : L_TEMP0;
		return 0;
	} else if (type == hwmon_fan && attr == hwmon_fan_label) {
		*str = L_FAN;
		return 0;
	} else if (type == hwmon_power && attr == hwmon_power_label && channel < 4) {
		*str = label_watts[channel];
		return 0;
	} else if (type == hwmon_in && attr == hwmon_in_label && channel < 4) {
		*str = label_volts[channel];
		return 0;
	} else if (type == hwmon_curr && attr == hwmon_curr_label && channel < 4) {
		*str = label_amps[channel];
		return 0;
	}

	return -EOPNOTSUPP;
}

static const struct hwmon_ops corsairpsu_hwmon_ops = {
	.is_visible	= corsairpsu_hwmon_ops_is_visible,
	.read		= corsairpsu_hwmon_ops_read,
	.read_string	= corsairpsu_hwmon_ops_read_string,
};

static const struct hwmon_channel_info *corsairpsu_info[] = {
	HWMON_CHANNEL_INFO(chip,
			   HWMON_C_REGISTER_TZ),
	HWMON_CHANNEL_INFO(temp,
			   HWMON_T_INPUT | HWMON_T_LABEL | HWMON_T_CRIT,
			   HWMON_T_INPUT | HWMON_T_LABEL | HWMON_T_CRIT),
	HWMON_CHANNEL_INFO(fan,
			   HWMON_F_INPUT | HWMON_F_LABEL),
	HWMON_CHANNEL_INFO(power,
			   HWMON_P_INPUT | HWMON_P_LABEL,
			   HWMON_P_INPUT | HWMON_P_LABEL,
			   HWMON_P_INPUT | HWMON_P_LABEL,
			   HWMON_P_INPUT | HWMON_P_LABEL),
	HWMON_CHANNEL_INFO(in,
			   HWMON_I_INPUT | HWMON_I_LABEL,
			   HWMON_I_INPUT | HWMON_I_LABEL | HWMON_I_LCRIT | HWMON_I_CRIT,
			   HWMON_I_INPUT | HWMON_I_LABEL | HWMON_I_LCRIT | HWMON_I_CRIT,
			   HWMON_I_INPUT | HWMON_I_LABEL | HWMON_I_LCRIT | HWMON_I_CRIT),
	HWMON_CHANNEL_INFO(curr,
			   HWMON_C_INPUT | HWMON_C_LABEL,
			   HWMON_C_INPUT | HWMON_C_LABEL | HWMON_C_CRIT,
			   HWMON_C_INPUT | HWMON_C_LABEL | HWMON_C_CRIT,
			   HWMON_C_INPUT | HWMON_C_LABEL | HWMON_C_CRIT),
	NULL
};

static const struct hwmon_chip_info corsairpsu_chip_info = {
	.ops	= &corsairpsu_hwmon_ops,
	.info	= corsairpsu_info,
};

#ifdef CONFIG_DEBUG_FS

static void print_uptime(struct seq_file *seqf, u8 cmd)
{
	struct corsairpsu_data *priv = seqf->private;
	long val;
	int ret;

	ret = corsairpsu_get_value(priv, cmd, 0, &val);
	if (ret < 0) {
		seq_puts(seqf, "N/A\n");
		return;
	}

	if (val > SECONDS_PER_DAY) {
		seq_printf(seqf, "%ld day(s), %02ld:%02ld:%02ld\n", val / SECONDS_PER_DAY,
			   val % SECONDS_PER_DAY / SECONDS_PER_HOUR, val % SECONDS_PER_HOUR / 60,
			   val % 60);
		return;
	}

	seq_printf(seqf, "%02ld:%02ld:%02ld\n", val % SECONDS_PER_DAY / SECONDS_PER_HOUR,
		   val % SECONDS_PER_HOUR / 60, val % 60);
}

static int uptime_show(struct seq_file *seqf, void *unused)
{
	print_uptime(seqf, PSU_CMD_UPTIME);

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(uptime);

static int uptime_total_show(struct seq_file *seqf, void *unused)
{
	print_uptime(seqf, PSU_CMD_TOTAL_UPTIME);

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(uptime_total);

static int vendor_show(struct seq_file *seqf, void *unused)
{
	struct corsairpsu_data *priv = seqf->private;

	seq_printf(seqf, "%s\n", priv->vendor);

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(vendor);

static int product_show(struct seq_file *seqf, void *unused)
{
	struct corsairpsu_data *priv = seqf->private;

	seq_printf(seqf, "%s\n", priv->product);

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(product);

static void corsairpsu_debugfs_init(struct corsairpsu_data *priv)
{
	char name[32];

	scnprintf(name, sizeof(name), "%s-%s", DRIVER_NAME, dev_name(&priv->hdev->dev));

	priv->debugfs = debugfs_create_dir(name, NULL);
	debugfs_create_file("uptime", 0444, priv->debugfs, priv, &uptime_fops);
	debugfs_create_file("uptime_total", 0444, priv->debugfs, priv, &uptime_total_fops);
	debugfs_create_file("vendor", 0444, priv->debugfs, priv, &vendor_fops);
	debugfs_create_file("product", 0444, priv->debugfs, priv, &product_fops);
}

#else

static void corsairpsu_debugfs_init(struct corsairpsu_data *priv)
{
}

#endif

static int corsairpsu_probe(struct hid_device *hdev, const struct hid_device_id *id)
{
	struct corsairpsu_data *priv;
	int ret;

	priv = devm_kzalloc(&hdev->dev, sizeof(struct corsairpsu_data), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->cmd_buffer = devm_kmalloc(&hdev->dev, CMD_BUFFER_SIZE, GFP_KERNEL);
	if (!priv->cmd_buffer)
		return -ENOMEM;

	ret = hid_parse(hdev);
	if (ret)
		return ret;

	ret = hid_hw_start(hdev, HID_CONNECT_HIDRAW);
	if (ret)
		return ret;

	ret = hid_hw_open(hdev);
	if (ret)
		goto fail_and_stop;

	priv->hdev = hdev;
	hid_set_drvdata(hdev, priv);
	mutex_init(&priv->lock);
	init_completion(&priv->wait_completion);

	hid_device_io_start(hdev);

	ret = corsairpsu_init(priv);
	if (ret < 0) {
		dev_err(&hdev->dev, "unable to initialize device (%d)\n", ret);
		goto fail_and_stop;
	}

	ret = corsairpsu_fwinfo(priv);
	if (ret < 0) {
		dev_err(&hdev->dev, "unable to query firmware (%d)\n", ret);
		goto fail_and_stop;
	}

	corsairpsu_get_criticals(priv);
	corsairpsu_check_cmd_support(priv);

	priv->hwmon_dev = hwmon_device_register_with_info(&hdev->dev, "corsairpsu", priv,
							  &corsairpsu_chip_info, 0);

	if (IS_ERR(priv->hwmon_dev)) {
		ret = PTR_ERR(priv->hwmon_dev);
		goto fail_and_close;
	}

	corsairpsu_debugfs_init(priv);

	return 0;

fail_and_close:
	hid_hw_close(hdev);
fail_and_stop:
	hid_hw_stop(hdev);
	return ret;
}

static void corsairpsu_remove(struct hid_device *hdev)
{
	struct corsairpsu_data *priv = hid_get_drvdata(hdev);

	debugfs_remove_recursive(priv->debugfs);
	hwmon_device_unregister(priv->hwmon_dev);
	hid_hw_close(hdev);
	hid_hw_stop(hdev);
}

static int corsairpsu_raw_event(struct hid_device *hdev, struct hid_report *report, u8 *data,
				int size)
{
	struct corsairpsu_data *priv = hid_get_drvdata(hdev);

	if (completion_done(&priv->wait_completion))
		return 0;

	memcpy(priv->cmd_buffer, data, min(CMD_BUFFER_SIZE, size));
	complete(&priv->wait_completion);

	return 0;
}

#ifdef CONFIG_PM
static int corsairpsu_resume(struct hid_device *hdev)
{
	struct corsairpsu_data *priv = hid_get_drvdata(hdev);

	/* some PSUs turn off the microcontroller during standby, so a reinit is required */
	return corsairpsu_init(priv);
}
#endif

static const struct hid_device_id corsairpsu_idtable[] = {
	{ HID_USB_DEVICE(0x1b1c, 0x1c03) }, /* Corsair HX550i */
	{ HID_USB_DEVICE(0x1b1c, 0x1c04) }, /* Corsair HX650i */
	{ HID_USB_DEVICE(0x1b1c, 0x1c05) }, /* Corsair HX750i */
	{ HID_USB_DEVICE(0x1b1c, 0x1c06) }, /* Corsair HX850i */
	{ HID_USB_DEVICE(0x1b1c, 0x1c07) }, /* Corsair HX1000i */
	{ HID_USB_DEVICE(0x1b1c, 0x1c08) }, /* Corsair HX1200i */
	{ HID_USB_DEVICE(0x1b1c, 0x1c09) }, /* Corsair RM550i */
	{ HID_USB_DEVICE(0x1b1c, 0x1c0a) }, /* Corsair RM650i */
	{ HID_USB_DEVICE(0x1b1c, 0x1c0b) }, /* Corsair RM750i */
	{ HID_USB_DEVICE(0x1b1c, 0x1c0c) }, /* Corsair RM850i */
	{ HID_USB_DEVICE(0x1b1c, 0x1c0d) }, /* Corsair RM1000i */
	{ },
};
MODULE_DEVICE_TABLE(hid, corsairpsu_idtable);

static struct hid_driver corsairpsu_driver = {
	.name		= DRIVER_NAME,
	.id_table	= corsairpsu_idtable,
	.probe		= corsairpsu_probe,
	.remove		= corsairpsu_remove,
	.raw_event	= corsairpsu_raw_event,
#ifdef CONFIG_PM
	.resume		= corsairpsu_resume,
	.reset_resume	= corsairpsu_resume,
#endif
};
module_hid_driver(corsairpsu_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Wilken Gottwalt <wilken.gottwalt@posteo.net>");
MODULE_DESCRIPTION("Linux driver for Corsair power supplies with HID sensors interface");
