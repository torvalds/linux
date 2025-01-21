// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2024 Inochi Amaoto <inochiama@outlook.com>
 *
 * Sophgo power control mcu for SG2042
 */

#include <linux/cleanup.h>
#include <linux/debugfs.h>
#include <linux/err.h>
#include <linux/hwmon.h>
#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>

/* fixed MCU registers */
#define REG_BOARD_TYPE				0x00
#define REG_MCU_FIRMWARE_VERSION		0x01
#define REG_PCB_VERSION				0x02
#define REG_PWR_CTRL				0x03
#define REG_SOC_TEMP				0x04
#define REG_BOARD_TEMP				0x05
#define REG_RST_COUNT				0x0a
#define REG_UPTIME				0x0b
#define REG_RESET_REASON			0x0d
#define REG_MCU_TYPE				0x18
#define REG_REPOWER_POLICY			0x65
#define REG_CRITICAL_TEMP			0x66
#define REG_REPOWER_TEMP			0x67

#define REPOWER_POLICY_REBOOT			1
#define REPOWER_POLICY_KEEP_OFF			2

#define MCU_POWER_MAX				0xff

#define DEFINE_MCU_DEBUG_ATTR(_name, _reg, _format)			\
	static int _name##_show(struct seq_file *seqf,			\
				    void *unused)			\
	{								\
		struct sg2042_mcu_data *mcu = seqf->private;		\
		int ret;						\
		ret = i2c_smbus_read_byte_data(mcu->client, (_reg));	\
		if (ret < 0)						\
			return ret;					\
		seq_printf(seqf, _format "\n", ret);			\
		return 0;						\
	}								\
	DEFINE_SHOW_ATTRIBUTE(_name)					\

struct sg2042_mcu_data {
	struct i2c_client	*client;
	struct dentry		*debugfs;
	struct mutex		mutex;
};

static struct dentry *sgmcu_debugfs;

static ssize_t reset_count_show(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	struct sg2042_mcu_data *mcu = dev_get_drvdata(dev);
	int ret;

	ret = i2c_smbus_read_byte_data(mcu->client, REG_RST_COUNT);
	if (ret < 0)
		return ret;

	return sprintf(buf, "%d\n", ret);
}

static ssize_t uptime_show(struct device *dev,
			   struct device_attribute *attr,
			   char *buf)
{
	struct sg2042_mcu_data *mcu = dev_get_drvdata(dev);
	u8 time_val[2];
	int ret;

	ret = i2c_smbus_read_i2c_block_data(mcu->client, REG_UPTIME,
					    sizeof(time_val), time_val);
	if (ret < 0)
		return ret;

	return sprintf(buf, "%d\n",
		       (time_val[0]) | (time_val[1] << 8));
}

static ssize_t reset_reason_show(struct device *dev,
				 struct device_attribute *attr,
				 char *buf)
{
	struct sg2042_mcu_data *mcu = dev_get_drvdata(dev);
	int ret;

	ret = i2c_smbus_read_byte_data(mcu->client, REG_RESET_REASON);
	if (ret < 0)
		return ret;

	return sprintf(buf, "0x%02x\n", ret);
}

static ssize_t repower_policy_show(struct device *dev,
				   struct device_attribute *attr,
				   char *buf)
{
	struct sg2042_mcu_data *mcu = dev_get_drvdata(dev);
	int ret;
	const char *action;

	ret = i2c_smbus_read_byte_data(mcu->client, REG_REPOWER_POLICY);
	if (ret < 0)
		return ret;

	if (ret == REPOWER_POLICY_REBOOT)
		action = "repower";
	else if (ret == REPOWER_POLICY_KEEP_OFF)
		action = "keep";
	else
		action = "unknown";

	return sprintf(buf, "%s\n", action);
}

static ssize_t repower_policy_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t count)
{
	struct sg2042_mcu_data *mcu = dev_get_drvdata(dev);
	u8 value;
	int ret;

	if (sysfs_streq("repower", buf))
		value = REPOWER_POLICY_REBOOT;
	else if (sysfs_streq("keep", buf))
		value = REPOWER_POLICY_KEEP_OFF;
	else
		return -EINVAL;

	ret = i2c_smbus_write_byte_data(mcu->client,
					REG_REPOWER_POLICY, value);
	if (ret < 0)
		return ret;

	return count;
}

static DEVICE_ATTR_RO(reset_count);
static DEVICE_ATTR_RO(uptime);
static DEVICE_ATTR_RO(reset_reason);
static DEVICE_ATTR_RW(repower_policy);

DEFINE_MCU_DEBUG_ATTR(firmware_version, REG_MCU_FIRMWARE_VERSION, "0x%02x");
DEFINE_MCU_DEBUG_ATTR(pcb_version, REG_PCB_VERSION, "0x%02x");
DEFINE_MCU_DEBUG_ATTR(board_type, REG_BOARD_TYPE, "0x%02x");
DEFINE_MCU_DEBUG_ATTR(mcu_type, REG_MCU_TYPE, "%d");

static struct attribute *sg2042_mcu_attrs[] = {
	&dev_attr_reset_count.attr,
	&dev_attr_uptime.attr,
	&dev_attr_reset_reason.attr,
	&dev_attr_repower_policy.attr,
	NULL
};

static const struct attribute_group sg2042_mcu_attr_group = {
	.attrs	= sg2042_mcu_attrs,
};

static const struct attribute_group *sg2042_mcu_groups[] = {
	&sg2042_mcu_attr_group,
	NULL
};

static const struct hwmon_channel_info * const sg2042_mcu_info[] = {
	HWMON_CHANNEL_INFO(chip, HWMON_C_REGISTER_TZ),
	HWMON_CHANNEL_INFO(temp, HWMON_T_INPUT | HWMON_T_CRIT |
					HWMON_T_CRIT_HYST,
				 HWMON_T_INPUT),
	NULL
};

static int sg2042_mcu_read(struct device *dev,
			   enum hwmon_sensor_types type,
			   u32 attr, int channel, long *val)
{
	struct sg2042_mcu_data *mcu = dev_get_drvdata(dev);
	int tmp;
	u8 reg;

	switch (attr) {
	case hwmon_temp_input:
		reg = channel ? REG_BOARD_TEMP : REG_SOC_TEMP;
		break;
	case hwmon_temp_crit:
		reg = REG_CRITICAL_TEMP;
		break;
	case hwmon_temp_crit_hyst:
		reg = REG_REPOWER_TEMP;
		break;
	default:
		return -EOPNOTSUPP;
	}

	tmp = i2c_smbus_read_byte_data(mcu->client, reg);
	if (tmp < 0)
		return tmp;
	*val = tmp * 1000;

	return 0;
}

static int sg2042_mcu_write(struct device *dev,
			    enum hwmon_sensor_types type,
			    u32 attr, int channel, long val)
{
	struct sg2042_mcu_data *mcu = dev_get_drvdata(dev);
	int temp = val / 1000;
	int hyst_temp, crit_temp;
	u8 reg;

	temp = clamp_val(temp, 0, MCU_POWER_MAX);

	guard(mutex)(&mcu->mutex);

	switch (attr) {
	case hwmon_temp_crit:
		hyst_temp = i2c_smbus_read_byte_data(mcu->client,
						     REG_REPOWER_TEMP);
		if (hyst_temp < 0)
			return hyst_temp;

		crit_temp = temp;
		reg = REG_CRITICAL_TEMP;
		break;
	case hwmon_temp_crit_hyst:
		crit_temp = i2c_smbus_read_byte_data(mcu->client,
						     REG_CRITICAL_TEMP);
		if (crit_temp < 0)
			return crit_temp;

		hyst_temp = temp;
		reg = REG_REPOWER_TEMP;
		break;
	default:
		return -EOPNOTSUPP;
	}

	/*
	 * ensure hyst_temp is smaller to avoid MCU from
	 * keeping triggering repower event.
	 */
	if (crit_temp < hyst_temp)
		return -EINVAL;

	return i2c_smbus_write_byte_data(mcu->client, reg, temp);
}

static umode_t sg2042_mcu_is_visible(const void *_data,
				     enum hwmon_sensor_types type,
				     u32 attr, int channel)
{
	switch (type) {
	case hwmon_temp:
		switch (attr) {
		case hwmon_temp_input:
			return 0444;
		case hwmon_temp_crit:
		case hwmon_temp_crit_hyst:
			if (channel == 0)
				return 0644;
			break;
		default:
			break;
		}
		break;
	default:
			break;
	}
	return 0;
}

static const struct hwmon_ops sg2042_mcu_ops = {
	.is_visible = sg2042_mcu_is_visible,
	.read = sg2042_mcu_read,
	.write = sg2042_mcu_write,
};

static const struct hwmon_chip_info sg2042_mcu_chip_info = {
	.ops = &sg2042_mcu_ops,
	.info = sg2042_mcu_info,
};

static void sg2042_mcu_debugfs_init(struct sg2042_mcu_data *mcu,
				    struct device *dev)
{
	mcu->debugfs = debugfs_create_dir(dev_name(dev), sgmcu_debugfs);

	debugfs_create_file("firmware_version", 0444, mcu->debugfs,
			    mcu, &firmware_version_fops);
	debugfs_create_file("pcb_version", 0444, mcu->debugfs, mcu,
			    &pcb_version_fops);
	debugfs_create_file("mcu_type", 0444, mcu->debugfs, mcu,
			    &mcu_type_fops);
	debugfs_create_file("board_type", 0444, mcu->debugfs, mcu,
			    &board_type_fops);
}

static int sg2042_mcu_i2c_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct sg2042_mcu_data *mcu;
	struct device *hwmon_dev;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_BYTE_DATA |
						I2C_FUNC_SMBUS_BLOCK_DATA))
		return -ENODEV;

	mcu = devm_kmalloc(dev, sizeof(*mcu), GFP_KERNEL);
	if (!mcu)
		return -ENOMEM;

	mutex_init(&mcu->mutex);
	mcu->client = client;

	i2c_set_clientdata(client, mcu);

	hwmon_dev = devm_hwmon_device_register_with_info(dev, "sg2042_mcu",
							 mcu,
							 &sg2042_mcu_chip_info,
							 NULL);
	if (IS_ERR(hwmon_dev))
		return PTR_ERR(hwmon_dev);

	sg2042_mcu_debugfs_init(mcu, dev);

	return 0;
}

static void sg2042_mcu_i2c_remove(struct i2c_client *client)
{
	struct sg2042_mcu_data *mcu = i2c_get_clientdata(client);

	debugfs_remove_recursive(mcu->debugfs);
}

static const struct i2c_device_id sg2042_mcu_id[] = {
	{ "sg2042-hwmon-mcu" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, sg2042_mcu_id);

static const struct of_device_id sg2042_mcu_of_id[] = {
	{ .compatible = "sophgo,sg2042-hwmon-mcu" },
	{},
};
MODULE_DEVICE_TABLE(of, sg2042_mcu_of_id);

static struct i2c_driver sg2042_mcu_driver = {
	.driver = {
		.name = "sg2042-mcu",
		.of_match_table = sg2042_mcu_of_id,
		.dev_groups = sg2042_mcu_groups,
	},
	.probe = sg2042_mcu_i2c_probe,
	.remove = sg2042_mcu_i2c_remove,
	.id_table = sg2042_mcu_id,
};

static int __init sg2042_mcu_init(void)
{
	sgmcu_debugfs = debugfs_create_dir("sg2042-mcu", NULL);
	return i2c_add_driver(&sg2042_mcu_driver);
}

static void __exit sg2042_mcu_exit(void)
{
	debugfs_remove_recursive(sgmcu_debugfs);
	i2c_del_driver(&sg2042_mcu_driver);
}

module_init(sg2042_mcu_init);
module_exit(sg2042_mcu_exit);

MODULE_AUTHOR("Inochi Amaoto <inochiama@outlook.com>");
MODULE_DESCRIPTION("MCU I2C driver for SG2042 soc platform");
MODULE_LICENSE("GPL");
