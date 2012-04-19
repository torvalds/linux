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
#define JC42_CFG_HYST_MASK	0x03

/* Capabilities */
#define JC42_CAP_RANGE		(1 << 2)

/* Manufacturer IDs */
#define ADT_MANID		0x11d4  /* Analog Devices */
#define ATMEL_MANID		0x001f  /* Atmel */
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
	{ IDT_MANID, TS3000B3_DEVID, TS3000B3_DEVID_MASK },
	{ IDT_MANID, TS3000GB2_DEVID, TS3000GB2_DEVID_MASK },
	{ MAX_MANID, MAX6604_DEVID, MAX6604_DEVID_MASK },
	{ MCP_MANID, MCP9804_DEVID, MCP9804_DEVID_MASK },
	{ MCP_MANID, MCP98242_DEVID, MCP98242_DEVID_MASK },
	{ MCP_MANID, MCP98243_DEVID, MCP98243_DEVID_MASK },
	{ MCP_MANID, MCP9843_DEVID, MCP9843_DEVID_MASK },
	{ NXP_MANID, SE97_DEVID, SE97_DEVID_MASK },
	{ ONS_MANID, CAT6095_DEVID, CAT6095_DEVID_MASK },
	{ NXP_MANID, SE98_DEVID, SE98_DEVID_MASK },
	{ STM_MANID, STTS424_DEVID, STTS424_DEVID_MASK },
	{ STM_MANID, STTS424E_DEVID, STTS424E_DEVID_MASK },
	{ STM_MANID, STTS2002_DEVID, STTS2002_DEVID_MASK },
	{ STM_MANID, STTS3000_DEVID, STTS3000_DEVID_MASK },
};

/* Each client has this additional data */
struct jc42_data {
	struct device	*hwmon_dev;
	struct mutex	update_lock;	/* protect register access */
	bool		extended;	/* true if extended range supported */
	bool		valid;
	unsigned long	last_updated;	/* In jiffies */
	u16		orig_config;	/* original configuration */
	u16		config;		/* current configuration */
	u16		temp_input;	/* Temperatures */
	u16		temp_crit;
	u16		temp_min;
	u16		temp_max;
};

static int jc42_probe(struct i2c_client *client,
		      const struct i2c_device_id *id);
static int jc42_detect(struct i2c_client *client, struct i2c_board_info *info);
static int jc42_remove(struct i2c_client *client);
static int jc42_read_value(struct i2c_client *client, u8 reg);
static int jc42_write_value(struct i2c_client *client, u8 reg, u16 value);

static struct jc42_data *jc42_update_device(struct device *dev);

static const struct i2c_device_id jc42_id[] = {
	{ "adt7408", 0 },
	{ "at30ts00", 0 },
	{ "cat94ts02", 0 },
	{ "cat6095", 0 },
	{ "jc42", 0 },
	{ "max6604", 0 },
	{ "mcp9804", 0 },
	{ "mcp9805", 0 },
	{ "mcp98242", 0 },
	{ "mcp98243", 0 },
	{ "mcp9843", 0 },
	{ "se97", 0 },
	{ "se97b", 0 },
	{ "se98", 0 },
	{ "stts424", 0 },
	{ "stts2002", 0 },
	{ "stts3000", 0 },
	{ "tse2002", 0 },
	{ "ts3000", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, jc42_id);

#ifdef CONFIG_PM

static int jc42_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct jc42_data *data = i2c_get_clientdata(client);

	data->config |= JC42_CFG_SHUTDOWN;
	jc42_write_value(client, JC42_REG_CONFIG, data->config);
	return 0;
}

static int jc42_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct jc42_data *data = i2c_get_clientdata(client);

	data->config &= ~JC42_CFG_SHUTDOWN;
	jc42_write_value(client, JC42_REG_CONFIG, data->config);
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

/* This is the driver that will be inserted */
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

#define JC42_TEMP_MIN_EXTENDED	(-40000)
#define JC42_TEMP_MIN		0
#define JC42_TEMP_MAX		125000

static u16 jc42_temp_to_reg(int temp, bool extended)
{
	int ntemp = SENSORS_LIMIT(temp,
				  extended ? JC42_TEMP_MIN_EXTENDED :
				  JC42_TEMP_MIN, JC42_TEMP_MAX);

	/* convert from 0.001 to 0.0625 resolution */
	return (ntemp * 2 / 125) & 0x1fff;
}

static int jc42_temp_from_reg(s16 reg)
{
	reg &= 0x1fff;

	/* sign extend register */
	if (reg & 0x1000)
		reg |= 0xf000;

	/* convert from 0.0625 to 0.001 resolution */
	return reg * 125 / 2;
}

/* sysfs stuff */

/* read routines for temperature limits */
#define show(value)	\
static ssize_t show_##value(struct device *dev,				\
			    struct device_attribute *attr,		\
			    char *buf)					\
{									\
	struct jc42_data *data = jc42_update_device(dev);		\
	if (IS_ERR(data))						\
		return PTR_ERR(data);					\
	return sprintf(buf, "%d\n", jc42_temp_from_reg(data->value));	\
}

show(temp_input);
show(temp_crit);
show(temp_min);
show(temp_max);

/* read routines for hysteresis values */
static ssize_t show_temp_crit_hyst(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct jc42_data *data = jc42_update_device(dev);
	int temp, hyst;

	if (IS_ERR(data))
		return PTR_ERR(data);

	temp = jc42_temp_from_reg(data->temp_crit);
	hyst = jc42_hysteresis[(data->config >> JC42_CFG_HYST_SHIFT)
			       & JC42_CFG_HYST_MASK];
	return sprintf(buf, "%d\n", temp - hyst);
}

static ssize_t show_temp_max_hyst(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct jc42_data *data = jc42_update_device(dev);
	int temp, hyst;

	if (IS_ERR(data))
		return PTR_ERR(data);

	temp = jc42_temp_from_reg(data->temp_max);
	hyst = jc42_hysteresis[(data->config >> JC42_CFG_HYST_SHIFT)
			       & JC42_CFG_HYST_MASK];
	return sprintf(buf, "%d\n", temp - hyst);
}

/* write routines */
#define set(value, reg)	\
static ssize_t set_##value(struct device *dev,				\
			   struct device_attribute *attr,		\
			   const char *buf, size_t count)		\
{									\
	struct i2c_client *client = to_i2c_client(dev);			\
	struct jc42_data *data = i2c_get_clientdata(client);		\
	int err, ret = count;						\
	long val;							\
	if (strict_strtol(buf, 10, &val) < 0)				\
		return -EINVAL;						\
	mutex_lock(&data->update_lock);					\
	data->value = jc42_temp_to_reg(val, data->extended);		\
	err = jc42_write_value(client, reg, data->value);		\
	if (err < 0)							\
		ret = err;						\
	mutex_unlock(&data->update_lock);				\
	return ret;							\
}

set(temp_min, JC42_REG_TEMP_LOWER);
set(temp_max, JC42_REG_TEMP_UPPER);
set(temp_crit, JC42_REG_TEMP_CRITICAL);

/* JC42.4 compliant chips only support four hysteresis values.
 * Pick best choice and go from there. */
static ssize_t set_temp_crit_hyst(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct jc42_data *data = i2c_get_clientdata(client);
	unsigned long val;
	int diff, hyst;
	int err;
	int ret = count;

	if (strict_strtoul(buf, 10, &val) < 0)
		return -EINVAL;

	diff = jc42_temp_from_reg(data->temp_crit) - val;
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
	data->config = (data->config
			& ~(JC42_CFG_HYST_MASK << JC42_CFG_HYST_SHIFT))
	  | (hyst << JC42_CFG_HYST_SHIFT);
	err = jc42_write_value(client, JC42_REG_CONFIG, data->config);
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

	val = data->temp_input;
	if (bit != JC42_ALARM_CRIT_BIT && (data->config & JC42_CFG_CRIT_ONLY))
		val = 0;
	return sprintf(buf, "%u\n", (val >> bit) & 1);
}

static DEVICE_ATTR(temp1_input, S_IRUGO,
		   show_temp_input, NULL);
static DEVICE_ATTR(temp1_crit, S_IRUGO,
		   show_temp_crit, set_temp_crit);
static DEVICE_ATTR(temp1_min, S_IRUGO,
		   show_temp_min, set_temp_min);
static DEVICE_ATTR(temp1_max, S_IRUGO,
		   show_temp_max, set_temp_max);

static DEVICE_ATTR(temp1_crit_hyst, S_IRUGO,
		   show_temp_crit_hyst, set_temp_crit_hyst);
static DEVICE_ATTR(temp1_max_hyst, S_IRUGO,
		   show_temp_max_hyst, NULL);

static SENSOR_DEVICE_ATTR(temp1_crit_alarm, S_IRUGO, show_alarm, NULL,
			  JC42_ALARM_CRIT_BIT);
static SENSOR_DEVICE_ATTR(temp1_min_alarm, S_IRUGO, show_alarm, NULL,
			  JC42_ALARM_MIN_BIT);
static SENSOR_DEVICE_ATTR(temp1_max_alarm, S_IRUGO, show_alarm, NULL,
			  JC42_ALARM_MAX_BIT);

static struct attribute *jc42_attributes[] = {
	&dev_attr_temp1_input.attr,
	&dev_attr_temp1_crit.attr,
	&dev_attr_temp1_min.attr,
	&dev_attr_temp1_max.attr,
	&dev_attr_temp1_crit_hyst.attr,
	&dev_attr_temp1_max_hyst.attr,
	&sensor_dev_attr_temp1_crit_alarm.dev_attr.attr,
	&sensor_dev_attr_temp1_min_alarm.dev_attr.attr,
	&sensor_dev_attr_temp1_max_alarm.dev_attr.attr,
	NULL
};

static mode_t jc42_attribute_mode(struct kobject *kobj,
				  struct attribute *attr, int index)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	struct i2c_client *client = to_i2c_client(dev);
	struct jc42_data *data = i2c_get_clientdata(client);
	unsigned int config = data->config;
	bool readonly;

	if (attr == &dev_attr_temp1_crit.attr)
		readonly = config & JC42_CFG_TCRIT_LOCK;
	else if (attr == &dev_attr_temp1_min.attr ||
		 attr == &dev_attr_temp1_max.attr)
		readonly = config & JC42_CFG_EVENT_LOCK;
	else if (attr == &dev_attr_temp1_crit_hyst.attr)
		readonly = config & (JC42_CFG_EVENT_LOCK | JC42_CFG_TCRIT_LOCK);
	else
		readonly = true;

	return S_IRUGO | (readonly ? 0 : S_IWUSR);
}

static const struct attribute_group jc42_group = {
	.attrs = jc42_attributes,
	.is_visible = jc42_attribute_mode,
};

/* Return 0 if detection is successful, -ENODEV otherwise */
static int jc42_detect(struct i2c_client *new_client,
		       struct i2c_board_info *info)
{
	struct i2c_adapter *adapter = new_client->adapter;
	int i, config, cap, manid, devid;

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA |
				     I2C_FUNC_SMBUS_WORD_DATA))
		return -ENODEV;

	cap = jc42_read_value(new_client, JC42_REG_CAP);
	config = jc42_read_value(new_client, JC42_REG_CONFIG);
	manid = jc42_read_value(new_client, JC42_REG_MANID);
	devid = jc42_read_value(new_client, JC42_REG_DEVICEID);

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

static int jc42_probe(struct i2c_client *new_client,
		      const struct i2c_device_id *id)
{
	struct jc42_data *data;
	int config, cap, err;

	data = kzalloc(sizeof(struct jc42_data), GFP_KERNEL);
	if (!data) {
		err = -ENOMEM;
		goto exit;
	}

	i2c_set_clientdata(new_client, data);
	mutex_init(&data->update_lock);

	cap = jc42_read_value(new_client, JC42_REG_CAP);
	if (cap < 0) {
		err = -EINVAL;
		goto exit_free;
	}
	data->extended = !!(cap & JC42_CAP_RANGE);

	config = jc42_read_value(new_client, JC42_REG_CONFIG);
	if (config < 0) {
		err = -EINVAL;
		goto exit_free;
	}
	data->orig_config = config;
	if (config & JC42_CFG_SHUTDOWN) {
		config &= ~JC42_CFG_SHUTDOWN;
		jc42_write_value(new_client, JC42_REG_CONFIG, config);
	}
	data->config = config;

	/* Register sysfs hooks */
	err = sysfs_create_group(&new_client->dev.kobj, &jc42_group);
	if (err)
		goto exit_free;

	data->hwmon_dev = hwmon_device_register(&new_client->dev);
	if (IS_ERR(data->hwmon_dev)) {
		err = PTR_ERR(data->hwmon_dev);
		goto exit_remove;
	}

	return 0;

exit_remove:
	sysfs_remove_group(&new_client->dev.kobj, &jc42_group);
exit_free:
	kfree(data);
exit:
	return err;
}

static int jc42_remove(struct i2c_client *client)
{
	struct jc42_data *data = i2c_get_clientdata(client);
	hwmon_device_unregister(data->hwmon_dev);
	sysfs_remove_group(&client->dev.kobj, &jc42_group);
	if (data->config != data->orig_config)
		jc42_write_value(client, JC42_REG_CONFIG, data->orig_config);
	kfree(data);
	return 0;
}

/* All registers are word-sized. */
static int jc42_read_value(struct i2c_client *client, u8 reg)
{
	int ret = i2c_smbus_read_word_data(client, reg);
	if (ret < 0)
		return ret;
	return swab16(ret);
}

static int jc42_write_value(struct i2c_client *client, u8 reg, u16 value)
{
	return i2c_smbus_write_word_data(client, reg, swab16(value));
}

static struct jc42_data *jc42_update_device(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct jc42_data *data = i2c_get_clientdata(client);
	struct jc42_data *ret = data;
	int val;

	mutex_lock(&data->update_lock);

	if (time_after(jiffies, data->last_updated + HZ) || !data->valid) {
		val = jc42_read_value(client, JC42_REG_TEMP);
		if (val < 0) {
			ret = ERR_PTR(val);
			goto abort;
		}
		data->temp_input = val;

		val = jc42_read_value(client, JC42_REG_TEMP_CRITICAL);
		if (val < 0) {
			ret = ERR_PTR(val);
			goto abort;
		}
		data->temp_crit = val;

		val = jc42_read_value(client, JC42_REG_TEMP_LOWER);
		if (val < 0) {
			ret = ERR_PTR(val);
			goto abort;
		}
		data->temp_min = val;

		val = jc42_read_value(client, JC42_REG_TEMP_UPPER);
		if (val < 0) {
			ret = ERR_PTR(val);
			goto abort;
		}
		data->temp_max = val;

		data->last_updated = jiffies;
		data->valid = true;
	}
abort:
	mutex_unlock(&data->update_lock);
	return ret;
}

static int __init sensors_jc42_init(void)
{
	return i2c_add_driver(&jc42_driver);
}

static void __exit sensors_jc42_exit(void)
{
	i2c_del_driver(&jc42_driver);
}

MODULE_AUTHOR("Guenter Roeck <guenter.roeck@ericsson.com>");
MODULE_DESCRIPTION("JC42 driver");
MODULE_LICENSE("GPL");

module_init(sensors_jc42_init);
module_exit(sensors_jc42_exit);
