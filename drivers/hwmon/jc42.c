/*
 * jc42.c - driver for Jedec JC42.4 compliant temperature sensors
 *
 * Copyright (c) 2010  Ericsson AB.
 *
 * Derived from lm77.c by Andras BALI <drewie@freemail.hu>.
 *
 * JC42.4 compliant temperature sensors are typically used on memory modules.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/jiffies.h>
#include <linux/i2c.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/err.h>
#include <linux/mutex.h>

/* Addresses to scan */
static const unsigned short normal_i2c[] = {
	0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f, I2C_CLIENT_END };

/* JC42 registers. All registers are 16 bit. */
#define JC42_REG_CAP		0x00
#define JC42_REG_CONFIG		0x01
#define JC42_REG_TEMP_UPPER	0x02
#define JC42_REG_TEMP_LOWER	0x03
#define JC42_REG_TEMP_CRITICAL	0x04
#define JC42_REG_TEMP		0x05
#define JC42_REG_MANID		0x06
#define JC42_REG_DEVICEID	0x07

/* Status bits in temperature register */
#define JC42_ALARM_CRIT_BIT	15
#define JC42_ALARM_MAX_BIT	14
#define JC42_ALARM_MIN_BIT	13

/* Configuration register defines */
#define JC42_CFG_CRIT_ONLY	(1 << 2)
#define JC42_CFG_TCRIT_LOCK	(1 << 6)
#define JC42_CFG_EVENT_LOCK	(1 << 7)
#define JC42_CFG_SHUTDOWN	(1 << 8)
#define JC42_CFG_HYST_SHIFT	9
#define JC42_CFG_HYST_MASK	(0x03 << 9)

/* Capabilities */
#define JC42_CAP_RANGE		(1 << 2)

/* Manufacturer IDs */
#define ADT_MANID		0x11d4  /* Analog Devices */
#define ATMEL_MANID		0x001f  /* Atmel */
#define ATMEL_MANID2		0x1114	/* Atmel */
#define MAX_MANID		0x004d  /* Maxim */
#define IDT_MANID		0x00b3  /* IDT */
#define MCP_MANID		0x0054  /* Microchip */
#define NXP_MANID		0x1131  /* NXP Semiconductors */
#define ONS_MANID		0x1b09  /* ON Semiconductor */
#define STM_MANID		0x104a  /* ST Microelectronics */

/* Supported chips */

/* Analog Devices */
#define ADT7408_DEVID		0x0801
#define ADT7408_DEVID_MASK	0xffff

/* Atmel */
#define AT30TS00_DEVID		0x8201
#define AT30TS00_DEVID_MASK	0xffff

#define AT30TSE004_DEVID	0x2200
#define AT30TSE004_DEVID_MASK	0xffff

/* IDT */
#define TS3000B3_DEVID		0x2903  /* Also matches TSE2002B3 */
#define TS3000B3_DEVID_MASK	0xffff

#define TS3000GB2_DEVID		0x2912  /* Also matches TSE2002GB2 */
#define TS3000GB2_DEVID_MASK	0xffff

/* Maxim */
#define MAX6604_DEVID		0x3e00
#define MAX6604_DEVID_MASK	0xffff

/* Microchip */
#define MCP9804_DEVID		0x0200
#define MCP9804_DEVID_MASK	0xfffc

#define MCP98242_DEVID		0x2000
#define MCP98242_DEVID_MASK	0xfffc

#define MCP98243_DEVID		0x2100
#define MCP98243_DEVID_MASK	0xfffc

#define MCP98244_DEVID		0x2200
#define MCP98244_DEVID_MASK	0xfffc

#define MCP9843_DEVID		0x0000	/* Also matches mcp9805 */
#define MCP9843_DEVID_MASK	0xfffe

/* NXP */
#define SE97_DEVID		0xa200
#define SE97_DEVID_MASK		0xfffc

#define SE98_DEVID		0xa100
#define SE98_DEVID_MASK		0xfffc

/* ON Semiconductor */
#define CAT6095_DEVID		0x0800	/* Also matches CAT34TS02 */
#define CAT6095_DEVID_MASK	0xffe0

/* ST Microelectronics */
#define STTS424_DEVID		0x0101
#define STTS424_DEVID_MASK	0xffff

#define STTS424E_DEVID		0x0000
#define STTS424E_DEVID_MASK	0xfffe

#define STTS2002_DEVID		0x0300
#define STTS2002_DEVID_MASK	0xffff

#define STTS2004_DEVID		0x2201
#define STTS2004_DEVID_MASK	0xffff

#define STTS3000_DEVID		0x0200
#define STTS3000_DEVID_MASK	0xffff

static u16 jc42_hysteresis[] = { 0, 1500, 3000, 6000 };

struct jc42_chips {
	u16 manid;
	u16 devid;
	u16 devid_mask;
};

static struct jc42_chips jc42_chips[] = {
	{ ADT_MANID, ADT7408_DEVID, ADT7408_DEVID_MASK },
	{ ATMEL_MANID, AT30TS00_DEVID, AT30TS00_DEVID_MASK },
	{ ATMEL_MANID2, AT30TSE004_DEVID, AT30TSE004_DEVID_MASK },
	{ IDT_MANID, TS3000B3_DEVID, TS3000B3_DEVID_MASK },
	{ IDT_MANID, TS3000GB2_DEVID, TS3000GB2_DEVID_MASK },
	{ MAX_MANID, MAX6604_DEVID, MAX6604_DEVID_MASK },
	{ MCP_MANID, MCP9804_DEVID, MCP9804_DEVID_MASK },
	{ MCP_MANID, MCP98242_DEVID, MCP98242_DEVID_MASK },
	{ MCP_MANID, MCP98243_DEVID, MCP98243_DEVID_MASK },
	{ MCP_MANID, MCP98244_DEVID, MCP98244_DEVID_MASK },
	{ MCP_MANID, MCP9843_DEVID, MCP9843_DEVID_MASK },
	{ NXP_MANID, SE97_DEVID, SE97_DEVID_MASK },
	{ ONS_MANID, CAT6095_DEVID, CAT6095_DEVID_MASK },
	{ NXP_MANID, SE98_DEVID, SE98_DEVID_MASK },
	{ STM_MANID, STTS424_DEVID, STTS424_DEVID_MASK },
	{ STM_MANID, STTS424E_DEVID, STTS424E_DEVID_MASK },
	{ STM_MANID, STTS2002_DEVID, STTS2002_DEVID_MASK },
	{ STM_MANID, STTS2004_DEVID, STTS2004_DEVID_MASK },
	{ STM_MANID, STTS3000_DEVID, STTS3000_DEVID_MASK },
};

enum temp_index {
	t_input = 0,
	t_crit,
	t_min,
	t_max,
	t_num_temp
};

static const u8 temp_regs[t_num_temp] = {
	[t_input] = JC42_REG_TEMP,
	[t_crit] = JC42_REG_TEMP_CRITICAL,
	[t_min] = JC42_REG_TEMP_LOWER,
	[t_max] = JC42_REG_TEMP_UPPER,
};

/* Each client has this additional data */
struct jc42_data {
	struct i2c_client *client;
	struct mutex	update_lock;	/* protect register access */
	bool		extended;	/* true if extended range supported */
	bool		valid;
	unsigned long	last_updated;	/* In jiffies */
	u16		orig_config;	/* original configuration */
	u16		config;		/* current configuration */
	u16		temp[t_num_temp];/* Temperatures */
};

#define JC42_TEMP_MIN_EXTENDED	(-40000)
#define JC42_TEMP_MIN		0
#define JC42_TEMP_MAX		125000

static u16 jc42_temp_to_reg(long temp, bool extended)
{
	int ntemp = clamp_val(temp,
			      extended ? JC42_TEMP_MIN_EXTENDED :
			      JC42_TEMP_MIN, JC42_TEMP_MAX);

	/* convert from 0.001 to 0.0625 resolution */
	return (ntemp * 2 / 125) & 0x1fff;
}

static int jc42_temp_from_reg(s16 reg)
{
	reg = sign_extend32(reg, 12);

	/* convert from 0.0625 to 0.001 resolution */
	return reg * 125 / 2;
}

static struct jc42_data *jc42_update_device(struct device *dev)
{
	struct jc42_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	struct jc42_data *ret = data;
	int i, val;

	mutex_lock(&data->update_lock);

	if (time_after(jiffies, data->last_updated + HZ) || !data->valid) {
		for (i = 0; i < t_num_temp; i++) {
			val = i2c_smbus_read_word_swapped(client, temp_regs[i]);
			if (val < 0) {
				ret = ERR_PTR(val);
				goto abort;
			}
			data->temp[i] = val;
		}
		data->last_updated = jiffies;
		data->valid = true;
	}
abort:
	mutex_unlock(&data->update_lock);
	return ret;
}

/* sysfs functions */

static ssize_t show_temp(struct device *dev, struct device_attribute *devattr,
			 char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct jc42_data *data = jc42_update_device(dev);
	if (IS_ERR(data))
		return PTR_ERR(data);
	return sprintf(buf, "%d\n",
		       jc42_temp_from_reg(data->temp[attr->index]));
}

static ssize_t show_temp_hyst(struct device *dev,
			      struct device_attribute *devattr, char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct jc42_data *data = jc42_update_device(dev);
	int temp, hyst;

	if (IS_ERR(data))
		return PTR_ERR(data);

	temp = jc42_temp_from_reg(data->temp[attr->index]);
	hyst = jc42_hysteresis[(data->config & JC42_CFG_HYST_MASK)
			       >> JC42_CFG_HYST_SHIFT];
	return sprintf(buf, "%d\n", temp - hyst);
}

static ssize_t set_temp(struct device *dev, struct device_attribute *devattr,
			const char *buf, size_t count)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct jc42_data *data = dev_get_drvdata(dev);
	int err, ret = count;
	int nr = attr->index;
	long val;

	if (kstrtol(buf, 10, &val) < 0)
		return -EINVAL;
	mutex_lock(&data->update_lock);
	data->temp[nr] = jc42_temp_to_reg(val, data->extended);
	err = i2c_smbus_write_word_swapped(data->client, temp_regs[nr],
					   data->temp[nr]);
	if (err < 0)
		ret = err;
	mutex_unlock(&data->update_lock);
	return ret;
}

/*
 * JC42.4 compliant chips only support four hysteresis values.
 * Pick best choice and go from there.
 */
static ssize_t set_temp_crit_hyst(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t count)
{
	struct jc42_data *data = dev_get_drvdata(dev);
	long val;
	int diff, hyst;
	int err;
	int ret = count;

	if (kstrtol(buf, 10, &val) < 0)
		return -EINVAL;

	val = clamp_val(val, (data->extended ? JC42_TEMP_MIN_EXTENDED :
			      JC42_TEMP_MIN) - 6000, JC42_TEMP_MAX);
	diff = jc42_temp_from_reg(data->temp[t_crit]) - val;

	hyst = 0;
	if (diff > 0) {
		if (diff < 2250)
			hyst = 1;	/* 1.5 degrees C */
		else if (diff < 4500)
			hyst = 2;	/* 3.0 degrees C */
		else
			hyst = 3;	/* 6.0 degrees C */
	}

	mutex_lock(&data->update_lock);
	data->config = (data->config & ~JC42_CFG_HYST_MASK)
	  | (hyst << JC42_CFG_HYST_SHIFT);
	err = i2c_smbus_write_word_swapped(data->client, JC42_REG_CONFIG,
					   data->config);
	if (err < 0)
		ret = err;
	mutex_unlock(&data->update_lock);
	return ret;
}

static ssize_t show_alarm(struct device *dev,
			  struct device_attribute *attr, char *buf)
{
	u16 bit = to_sensor_dev_attr(attr)->index;
	struct jc42_data *data = jc42_update_device(dev);
	u16 val;

	if (IS_ERR(data))
		return PTR_ERR(data);

	val = data->temp[t_input];
	if (bit != JC42_ALARM_CRIT_BIT && (data->config & JC42_CFG_CRIT_ONLY))
		val = 0;
	return sprintf(buf, "%u\n", (val >> bit) & 1);
}

static SENSOR_DEVICE_ATTR(temp1_input, S_IRUGO, show_temp, NULL, t_input);
static SENSOR_DEVICE_ATTR(temp1_crit, S_IRUGO, show_temp, set_temp, t_crit);
static SENSOR_DEVICE_ATTR(temp1_min, S_IRUGO, show_temp, set_temp, t_min);
static SENSOR_DEVICE_ATTR(temp1_max, S_IRUGO, show_temp, set_temp, t_max);

static SENSOR_DEVICE_ATTR(temp1_crit_hyst, S_IRUGO, show_temp_hyst,
			  set_temp_crit_hyst, t_crit);
static SENSOR_DEVICE_ATTR(temp1_max_hyst, S_IRUGO, show_temp_hyst, NULL, t_max);

static SENSOR_DEVICE_ATTR(temp1_crit_alarm, S_IRUGO, show_alarm, NULL,
			  JC42_ALARM_CRIT_BIT);
static SENSOR_DEVICE_ATTR(temp1_min_alarm, S_IRUGO, show_alarm, NULL,
			  JC42_ALARM_MIN_BIT);
static SENSOR_DEVICE_ATTR(temp1_max_alarm, S_IRUGO, show_alarm, NULL,
			  JC42_ALARM_MAX_BIT);

static struct attribute *jc42_attributes[] = {
	&sensor_dev_attr_temp1_input.dev_attr.attr,
	&sensor_dev_attr_temp1_crit.dev_attr.attr,
	&sensor_dev_attr_temp1_min.dev_attr.attr,
	&sensor_dev_attr_temp1_max.dev_attr.attr,
	&sensor_dev_attr_temp1_crit_hyst.dev_attr.attr,
	&sensor_dev_attr_temp1_max_hyst.dev_attr.attr,
	&sensor_dev_attr_temp1_crit_alarm.dev_attr.attr,
	&sensor_dev_attr_temp1_min_alarm.dev_attr.attr,
	&sensor_dev_attr_temp1_max_alarm.dev_attr.attr,
	NULL
};

static umode_t jc42_attribute_mode(struct kobject *kobj,
				  struct attribute *attr, int index)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	struct jc42_data *data = dev_get_drvdata(dev);
	unsigned int config = data->config;
	bool readonly;

	if (attr == &sensor_dev_attr_temp1_crit.dev_attr.attr)
		readonly = config & JC42_CFG_TCRIT_LOCK;
	else if (attr == &sensor_dev_attr_temp1_min.dev_attr.attr ||
		 attr == &sensor_dev_attr_temp1_max.dev_attr.attr)
		readonly = config & JC42_CFG_EVENT_LOCK;
	else if (attr == &sensor_dev_attr_temp1_crit_hyst.dev_attr.attr)
		readonly = config & (JC42_CFG_EVENT_LOCK | JC42_CFG_TCRIT_LOCK);
	else
		readonly = true;

	return S_IRUGO | (readonly ? 0 : S_IWUSR);
}

static const struct attribute_group jc42_group = {
	.attrs = jc42_attributes,
	.is_visible = jc42_attribute_mode,
};
__ATTRIBUTE_GROUPS(jc42);

/* Return 0 if detection is successful, -ENODEV otherwise */
static int jc42_detect(struct i2c_client *client, struct i2c_board_info *info)
{
	struct i2c_adapter *adapter = client->adapter;
	int i, config, cap, manid, devid;

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA |
				     I2C_FUNC_SMBUS_WORD_DATA))
		return -ENODEV;

	cap = i2c_smbus_read_word_swapped(client, JC42_REG_CAP);
	config = i2c_smbus_read_word_swapped(client, JC42_REG_CONFIG);
	manid = i2c_smbus_read_word_swapped(client, JC42_REG_MANID);
	devid = i2c_smbus_read_word_swapped(client, JC42_REG_DEVICEID);

	if (cap < 0 || config < 0 || manid < 0 || devid < 0)
		return -ENODEV;

	if ((cap & 0xff00) || (config & 0xf800))
		return -ENODEV;

	for (i = 0; i < ARRAY_SIZE(jc42_chips); i++) {
		struct jc42_chips *chip = &jc42_chips[i];
		if (manid == chip->manid &&
		    (devid & chip->devid_mask) == chip->devid) {
			strlcpy(info->type, "jc42", I2C_NAME_SIZE);
			return 0;
		}
	}
	return -ENODEV;
}

static int jc42_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct device *hwmon_dev;
	struct jc42_data *data;
	int config, cap;

	data = devm_kzalloc(dev, sizeof(struct jc42_data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->client = client;
	i2c_set_clientdata(client, data);
	mutex_init(&data->update_lock);

	cap = i2c_smbus_read_word_swapped(client, JC42_REG_CAP);
	if (cap < 0)
		return cap;

	data->extended = !!(cap & JC42_CAP_RANGE);

	config = i2c_smbus_read_word_swapped(client, JC42_REG_CONFIG);
	if (config < 0)
		return config;

	data->orig_config = config;
	if (config & JC42_CFG_SHUTDOWN) {
		config &= ~JC42_CFG_SHUTDOWN;
		i2c_smbus_write_word_swapped(client, JC42_REG_CONFIG, config);
	}
	data->config = config;

	hwmon_dev = devm_hwmon_device_register_with_groups(dev, client->name,
							   data,
							   jc42_groups);
	return PTR_ERR_OR_ZERO(hwmon_dev);
}

static int jc42_remove(struct i2c_client *client)
{
	struct jc42_data *data = i2c_get_clientdata(client);

	/* Restore original configuration except hysteresis */
	if ((data->config & ~JC42_CFG_HYST_MASK) !=
	    (data->orig_config & ~JC42_CFG_HYST_MASK)) {
		int config;

		config = (data->orig_config & ~JC42_CFG_HYST_MASK)
		  | (data->config & JC42_CFG_HYST_MASK);
		i2c_smbus_write_word_swapped(client, JC42_REG_CONFIG, config);
	}
	return 0;
}

#ifdef CONFIG_PM

static int jc42_suspend(struct device *dev)
{
	struct jc42_data *data = dev_get_drvdata(dev);

	data->config |= JC42_CFG_SHUTDOWN;
	i2c_smbus_write_word_swapped(data->client, JC42_REG_CONFIG,
				     data->config);
	return 0;
}

static int jc42_resume(struct device *dev)
{
	struct jc42_data *data = dev_get_drvdata(dev);

	data->config &= ~JC42_CFG_SHUTDOWN;
	i2c_smbus_write_word_swapped(data->client, JC42_REG_CONFIG,
				     data->config);
	return 0;
}

static const struct dev_pm_ops jc42_dev_pm_ops = {
	.suspend = jc42_suspend,
	.resume = jc42_resume,
};

#define JC42_DEV_PM_OPS (&jc42_dev_pm_ops)
#else
#define JC42_DEV_PM_OPS NULL
#endif /* CONFIG_PM */

static const struct i2c_device_id jc42_id[] = {
	{ "jc42", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, jc42_id);

static struct i2c_driver jc42_driver = {
	.class		= I2C_CLASS_SPD,
	.driver = {
		.name	= "jc42",
		.pm = JC42_DEV_PM_OPS,
	},
	.probe		= jc42_probe,
	.remove		= jc42_remove,
	.id_table	= jc42_id,
	.detect		= jc42_detect,
	.address_list	= normal_i2c,
};

module_i2c_driver(jc42_driver);

MODULE_AUTHOR("Guenter Roeck <linux@roeck-us.net>");
MODULE_DESCRIPTION("JC42 driver");
MODULE_LICENSE("GPL");
