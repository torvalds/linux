/*
 * ad525x_dpot: Driver for the Analog Devices AD525x digital potentiometers
 * Copyright (c) 2009 Analog Devices, Inc.
 * Author: Michael Hennerich <hennerich@blackfin.uclinux.org>
 *
 * DEVID		#Wipers		#Positions 	Resistor Options (kOhm)
 * AD5258		1		64		1, 10, 50, 100
 * AD5259		1		256		5, 10, 50, 100
 * AD5251		2		64		1, 10, 50, 100
 * AD5252		2		256		1, 10, 50, 100
 * AD5255		3		512		25, 250
 * AD5253		4		64		1, 10, 50, 100
 * AD5254		4		256		1, 10, 50, 100
 *
 * See Documentation/misc-devices/ad525x_dpot.txt for more info.
 *
 * derived from ad5258.c
 * Copyright (c) 2009 Cyber Switching, Inc.
 * Author: Chris Verges <chrisv@cyberswitching.com>
 *
 * derived from ad5252.c
 * Copyright (c) 2006 Michael Hennerich <hennerich@blackfin.uclinux.org>
 *
 * Licensed under the GPL-2 or later.
 */

#include <linux/module.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/delay.h>

#define DRIVER_NAME			"ad525x_dpot"
#define DRIVER_VERSION			"0.1"

enum dpot_devid {
	AD5258_ID,
	AD5259_ID,
	AD5251_ID,
	AD5252_ID,
	AD5253_ID,
	AD5254_ID,
	AD5255_ID,
};

#define AD5258_MAX_POSITION		64
#define AD5259_MAX_POSITION		256
#define AD5251_MAX_POSITION		64
#define AD5252_MAX_POSITION		256
#define AD5253_MAX_POSITION		64
#define AD5254_MAX_POSITION		256
#define AD5255_MAX_POSITION		512

#define AD525X_RDAC0		0
#define AD525X_RDAC1		1
#define AD525X_RDAC2		2
#define AD525X_RDAC3		3

#define AD525X_REG_TOL		0x18
#define AD525X_TOL_RDAC0	(AD525X_REG_TOL | AD525X_RDAC0)
#define AD525X_TOL_RDAC1	(AD525X_REG_TOL | AD525X_RDAC1)
#define AD525X_TOL_RDAC2	(AD525X_REG_TOL | AD525X_RDAC2)
#define AD525X_TOL_RDAC3	(AD525X_REG_TOL | AD525X_RDAC3)

/* RDAC-to-EEPROM Interface Commands */
#define AD525X_I2C_RDAC		(0x00 << 5)
#define AD525X_I2C_EEPROM	(0x01 << 5)
#define AD525X_I2C_CMD		(0x80)

#define AD525X_DEC_ALL_6DB	(AD525X_I2C_CMD | (0x4 << 3))
#define AD525X_INC_ALL_6DB	(AD525X_I2C_CMD | (0x9 << 3))
#define AD525X_DEC_ALL		(AD525X_I2C_CMD | (0x6 << 3))
#define AD525X_INC_ALL		(AD525X_I2C_CMD | (0xB << 3))

static s32 ad525x_read(struct i2c_client *client, u8 reg);
static s32 ad525x_write(struct i2c_client *client, u8 reg, u8 value);

/*
 * Client data (each client gets its own)
 */

struct dpot_data {
	struct mutex update_lock;
	unsigned rdac_mask;
	unsigned max_pos;
	unsigned devid;
};

/* sysfs functions */

static ssize_t sysfs_show_reg(struct device *dev,
			      struct device_attribute *attr, char *buf, u32 reg)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct dpot_data *data = i2c_get_clientdata(client);
	s32 value;

	mutex_lock(&data->update_lock);
	value = ad525x_read(client, reg);
	mutex_unlock(&data->update_lock);

	if (value < 0)
		return -EINVAL;
	/*
	 * Let someone else deal with converting this ...
	 * the tolerance is a two-byte value where the MSB
	 * is a sign + integer value, and the LSB is a
	 * decimal value.  See page 18 of the AD5258
	 * datasheet (Rev. A) for more details.
	 */

	if (reg & AD525X_REG_TOL)
		return sprintf(buf, "0x%04x\n", value & 0xFFFF);
	else
		return sprintf(buf, "%u\n", value & data->rdac_mask);
}

static ssize_t sysfs_set_reg(struct device *dev,
			     struct device_attribute *attr,
			     const char *buf, size_t count, u32 reg)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct dpot_data *data = i2c_get_clientdata(client);
	unsigned long value;
	int err;

	err = strict_strtoul(buf, 10, &value);
	if (err)
		return err;

	if (value > data->rdac_mask)
		value = data->rdac_mask;

	mutex_lock(&data->update_lock);
	ad525x_write(client, reg, value);
	if (reg & AD525X_I2C_EEPROM)
		msleep(26);	/* Sleep while the EEPROM updates */
	mutex_unlock(&data->update_lock);

	return count;
}

static ssize_t sysfs_do_cmd(struct device *dev,
			    struct device_attribute *attr,
			    const char *buf, size_t count, u32 reg)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct dpot_data *data = i2c_get_clientdata(client);

	mutex_lock(&data->update_lock);
	ad525x_write(client, reg, 0);
	mutex_unlock(&data->update_lock);

	return count;
}

/* ------------------------------------------------------------------------- */

static ssize_t show_rdac0(struct device *dev,
			  struct device_attribute *attr, char *buf)
{
	return sysfs_show_reg(dev, attr, buf, AD525X_I2C_RDAC | AD525X_RDAC0);
}

static ssize_t set_rdac0(struct device *dev,
			 struct device_attribute *attr,
			 const char *buf, size_t count)
{
	return sysfs_set_reg(dev, attr, buf, count,
			     AD525X_I2C_RDAC | AD525X_RDAC0);
}

static DEVICE_ATTR(rdac0, S_IWUSR | S_IRUGO, show_rdac0, set_rdac0);

static ssize_t show_eeprom0(struct device *dev,
			    struct device_attribute *attr, char *buf)
{
	return sysfs_show_reg(dev, attr, buf, AD525X_I2C_EEPROM | AD525X_RDAC0);
}

static ssize_t set_eeprom0(struct device *dev,
			   struct device_attribute *attr,
			   const char *buf, size_t count)
{
	return sysfs_set_reg(dev, attr, buf, count,
			     AD525X_I2C_EEPROM | AD525X_RDAC0);
}

static DEVICE_ATTR(eeprom0, S_IWUSR | S_IRUGO, show_eeprom0, set_eeprom0);

static ssize_t show_tolerance0(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	return sysfs_show_reg(dev, attr, buf,
			      AD525X_I2C_EEPROM | AD525X_TOL_RDAC0);
}

static DEVICE_ATTR(tolerance0, S_IRUGO, show_tolerance0, NULL);

/* ------------------------------------------------------------------------- */

static ssize_t show_rdac1(struct device *dev,
			  struct device_attribute *attr, char *buf)
{
	return sysfs_show_reg(dev, attr, buf, AD525X_I2C_RDAC | AD525X_RDAC1);
}

static ssize_t set_rdac1(struct device *dev,
			 struct device_attribute *attr,
			 const char *buf, size_t count)
{
	return sysfs_set_reg(dev, attr, buf, count,
			     AD525X_I2C_RDAC | AD525X_RDAC1);
}

static DEVICE_ATTR(rdac1, S_IWUSR | S_IRUGO, show_rdac1, set_rdac1);

static ssize_t show_eeprom1(struct device *dev,
			    struct device_attribute *attr, char *buf)
{
	return sysfs_show_reg(dev, attr, buf, AD525X_I2C_EEPROM | AD525X_RDAC1);
}

static ssize_t set_eeprom1(struct device *dev,
			   struct device_attribute *attr,
			   const char *buf, size_t count)
{
	return sysfs_set_reg(dev, attr, buf, count,
			     AD525X_I2C_EEPROM | AD525X_RDAC1);
}

static DEVICE_ATTR(eeprom1, S_IWUSR | S_IRUGO, show_eeprom1, set_eeprom1);

static ssize_t show_tolerance1(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	return sysfs_show_reg(dev, attr, buf,
			      AD525X_I2C_EEPROM | AD525X_TOL_RDAC1);
}

static DEVICE_ATTR(tolerance1, S_IRUGO, show_tolerance1, NULL);

/* ------------------------------------------------------------------------- */

static ssize_t show_rdac2(struct device *dev,
			  struct device_attribute *attr, char *buf)
{
	return sysfs_show_reg(dev, attr, buf, AD525X_I2C_RDAC | AD525X_RDAC2);
}

static ssize_t set_rdac2(struct device *dev,
			 struct device_attribute *attr,
			 const char *buf, size_t count)
{
	return sysfs_set_reg(dev, attr, buf, count,
			     AD525X_I2C_RDAC | AD525X_RDAC2);
}

static DEVICE_ATTR(rdac2, S_IWUSR | S_IRUGO, show_rdac2, set_rdac2);

static ssize_t show_eeprom2(struct device *dev,
			    struct device_attribute *attr, char *buf)
{
	return sysfs_show_reg(dev, attr, buf, AD525X_I2C_EEPROM | AD525X_RDAC2);
}

static ssize_t set_eeprom2(struct device *dev,
			   struct device_attribute *attr,
			   const char *buf, size_t count)
{
	return sysfs_set_reg(dev, attr, buf, count,
			     AD525X_I2C_EEPROM | AD525X_RDAC2);
}

static DEVICE_ATTR(eeprom2, S_IWUSR | S_IRUGO, show_eeprom2, set_eeprom2);

static ssize_t show_tolerance2(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	return sysfs_show_reg(dev, attr, buf,
			      AD525X_I2C_EEPROM | AD525X_TOL_RDAC2);
}

static DEVICE_ATTR(tolerance2, S_IRUGO, show_tolerance2, NULL);

/* ------------------------------------------------------------------------- */

static ssize_t show_rdac3(struct device *dev,
			  struct device_attribute *attr, char *buf)
{
	return sysfs_show_reg(dev, attr, buf, AD525X_I2C_RDAC | AD525X_RDAC3);
}

static ssize_t set_rdac3(struct device *dev,
			 struct device_attribute *attr,
			 const char *buf, size_t count)
{
	return sysfs_set_reg(dev, attr, buf, count,
			     AD525X_I2C_RDAC | AD525X_RDAC3);
}

static DEVICE_ATTR(rdac3, S_IWUSR | S_IRUGO, show_rdac3, set_rdac3);

static ssize_t show_eeprom3(struct device *dev,
			    struct device_attribute *attr, char *buf)
{
	return sysfs_show_reg(dev, attr, buf, AD525X_I2C_EEPROM | AD525X_RDAC3);
}

static ssize_t set_eeprom3(struct device *dev,
			   struct device_attribute *attr,
			   const char *buf, size_t count)
{
	return sysfs_set_reg(dev, attr, buf, count,
			     AD525X_I2C_EEPROM | AD525X_RDAC3);
}

static DEVICE_ATTR(eeprom3, S_IWUSR | S_IRUGO, show_eeprom3, set_eeprom3);

static ssize_t show_tolerance3(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	return sysfs_show_reg(dev, attr, buf,
			      AD525X_I2C_EEPROM | AD525X_TOL_RDAC3);
}

static DEVICE_ATTR(tolerance3, S_IRUGO, show_tolerance3, NULL);

static struct attribute *ad525x_attributes_wipers[4][4] = {
	{
		&dev_attr_rdac0.attr,
		&dev_attr_eeprom0.attr,
		&dev_attr_tolerance0.attr,
		NULL
	}, {
		&dev_attr_rdac1.attr,
		&dev_attr_eeprom1.attr,
		&dev_attr_tolerance1.attr,
		NULL
	}, {
		&dev_attr_rdac2.attr,
		&dev_attr_eeprom2.attr,
		&dev_attr_tolerance2.attr,
		NULL
	}, {
		&dev_attr_rdac3.attr,
		&dev_attr_eeprom3.attr,
		&dev_attr_tolerance3.attr,
		NULL
	}
};

static const struct attribute_group ad525x_group_wipers[] = {
	{.attrs = ad525x_attributes_wipers[AD525X_RDAC0]},
	{.attrs = ad525x_attributes_wipers[AD525X_RDAC1]},
	{.attrs = ad525x_attributes_wipers[AD525X_RDAC2]},
	{.attrs = ad525x_attributes_wipers[AD525X_RDAC3]},
};

/* ------------------------------------------------------------------------- */

static ssize_t set_inc_all(struct device *dev,
			   struct device_attribute *attr,
			   const char *buf, size_t count)
{
	return sysfs_do_cmd(dev, attr, buf, count, AD525X_INC_ALL);
}

static DEVICE_ATTR(inc_all, S_IWUSR, NULL, set_inc_all);

static ssize_t set_dec_all(struct device *dev,
			   struct device_attribute *attr,
			   const char *buf, size_t count)
{
	return sysfs_do_cmd(dev, attr, buf, count, AD525X_DEC_ALL);
}

static DEVICE_ATTR(dec_all, S_IWUSR, NULL, set_dec_all);

static ssize_t set_inc_all_6db(struct device *dev,
			       struct device_attribute *attr,
			       const char *buf, size_t count)
{
	return sysfs_do_cmd(dev, attr, buf, count, AD525X_INC_ALL_6DB);
}

static DEVICE_ATTR(inc_all_6db, S_IWUSR, NULL, set_inc_all_6db);

static ssize_t set_dec_all_6db(struct device *dev,
			       struct device_attribute *attr,
			       const char *buf, size_t count)
{
	return sysfs_do_cmd(dev, attr, buf, count, AD525X_DEC_ALL_6DB);
}

static DEVICE_ATTR(dec_all_6db, S_IWUSR, NULL, set_dec_all_6db);

static struct attribute *ad525x_attributes_commands[] = {
	&dev_attr_inc_all.attr,
	&dev_attr_dec_all.attr,
	&dev_attr_inc_all_6db.attr,
	&dev_attr_dec_all_6db.attr,
	NULL
};

static const struct attribute_group ad525x_group_commands = {
	.attrs = ad525x_attributes_commands,
};

/* ------------------------------------------------------------------------- */

/* i2c device functions */

/**
 * ad525x_read - return the value contained in the specified register
 * on the AD5258 device.
 * @client: value returned from i2c_new_device()
 * @reg: the register to read
 *
 * If the tolerance register is specified, 2 bytes are returned.
 * Otherwise, 1 byte is returned.  A negative value indicates an error
 * occurred while reading the register.
 */
static s32 ad525x_read(struct i2c_client *client, u8 reg)
{
	struct dpot_data *data = i2c_get_clientdata(client);

	if ((reg & AD525X_REG_TOL) || (data->max_pos > 256))
		return i2c_smbus_read_word_data(client, (reg & 0xF8) |
						((reg & 0x7) << 1));
	else
		return i2c_smbus_read_byte_data(client, reg);
}

/**
 * ad525x_write - store the given value in the specified register on
 * the AD5258 device.
 * @client: value returned from i2c_new_device()
 * @reg: the register to write
 * @value: the byte to store in the register
 *
 * For certain instructions that do not require a data byte, "NULL"
 * should be specified for the "value" parameter.  These instructions
 * include NOP, RESTORE_FROM_EEPROM, and STORE_TO_EEPROM.
 *
 * A negative return value indicates an error occurred while reading
 * the register.
 */
static s32 ad525x_write(struct i2c_client *client, u8 reg, u8 value)
{
	struct dpot_data *data = i2c_get_clientdata(client);

	/* Only write the instruction byte for certain commands */
	if (reg & AD525X_I2C_CMD)
		return i2c_smbus_write_byte(client, reg);

	if (data->max_pos > 256)
		return i2c_smbus_write_word_data(client, (reg & 0xF8) |
						((reg & 0x7) << 1), value);
	else
		/* All other registers require instruction + data bytes */
		return i2c_smbus_write_byte_data(client, reg, value);
}

static int ad525x_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct dpot_data *data;
	int err = 0;

	dev_dbg(dev, "%s\n", __func__);

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_BYTE)) {
		dev_err(dev, "missing I2C functionality for this driver\n");
		goto exit;
	}

	data = kzalloc(sizeof(struct dpot_data), GFP_KERNEL);
	if (!data) {
		err = -ENOMEM;
		goto exit;
	}

	i2c_set_clientdata(client, data);
	mutex_init(&data->update_lock);

	switch (id->driver_data) {
	case AD5258_ID:
		data->max_pos = AD5258_MAX_POSITION;
		err = sysfs_create_group(&dev->kobj,
				       &ad525x_group_wipers[AD525X_RDAC0]);
		break;
	case AD5259_ID:
		data->max_pos = AD5259_MAX_POSITION;
		err = sysfs_create_group(&dev->kobj,
				       &ad525x_group_wipers[AD525X_RDAC0]);
		break;
	case AD5251_ID:
		data->max_pos = AD5251_MAX_POSITION;
		err = sysfs_create_group(&dev->kobj,
				       &ad525x_group_wipers[AD525X_RDAC1]);
		err |= sysfs_create_group(&dev->kobj,
				       &ad525x_group_wipers[AD525X_RDAC3]);
		err |= sysfs_create_group(&dev->kobj, &ad525x_group_commands);
		break;
	case AD5252_ID:
		data->max_pos = AD5252_MAX_POSITION;
		err = sysfs_create_group(&dev->kobj,
				       &ad525x_group_wipers[AD525X_RDAC1]);
		err |= sysfs_create_group(&dev->kobj,
				       &ad525x_group_wipers[AD525X_RDAC3]);
		err |= sysfs_create_group(&dev->kobj, &ad525x_group_commands);
		break;
	case AD5253_ID:
		data->max_pos = AD5253_MAX_POSITION;
		err = sysfs_create_group(&dev->kobj,
				       &ad525x_group_wipers[AD525X_RDAC0]);
		err |= sysfs_create_group(&dev->kobj,
				       &ad525x_group_wipers[AD525X_RDAC1]);
		err |= sysfs_create_group(&dev->kobj,
				       &ad525x_group_wipers[AD525X_RDAC2]);
		err |= sysfs_create_group(&dev->kobj,
				       &ad525x_group_wipers[AD525X_RDAC3]);
		err |= sysfs_create_group(&dev->kobj, &ad525x_group_commands);
		break;
	case AD5254_ID:
		data->max_pos = AD5254_MAX_POSITION;
		err = sysfs_create_group(&dev->kobj,
				       &ad525x_group_wipers[AD525X_RDAC0]);
		err |= sysfs_create_group(&dev->kobj,
				       &ad525x_group_wipers[AD525X_RDAC1]);
		err |= sysfs_create_group(&dev->kobj,
				       &ad525x_group_wipers[AD525X_RDAC2]);
		err |= sysfs_create_group(&dev->kobj,
				       &ad525x_group_wipers[AD525X_RDAC3]);
		err |= sysfs_create_group(&dev->kobj, &ad525x_group_commands);
		break;
	case AD5255_ID:
		data->max_pos = AD5255_MAX_POSITION;
		err = sysfs_create_group(&dev->kobj,
				       &ad525x_group_wipers[AD525X_RDAC0]);
		err |= sysfs_create_group(&dev->kobj,
				       &ad525x_group_wipers[AD525X_RDAC1]);
		err |= sysfs_create_group(&dev->kobj,
				       &ad525x_group_wipers[AD525X_RDAC2]);
		err |= sysfs_create_group(&dev->kobj, &ad525x_group_commands);
		break;
	default:
		err = -ENODEV;
		goto exit_free;
	}

	if (err) {
		dev_err(dev, "failed to register sysfs hooks\n");
		goto exit_free;
	}

	data->devid = id->driver_data;
	data->rdac_mask = data->max_pos - 1;

	dev_info(dev, "%s %d-Position Digital Potentiometer registered\n",
		 id->name, data->max_pos);

	return 0;

exit_free:
	kfree(data);
	i2c_set_clientdata(client, NULL);
exit:
	dev_err(dev, "failed to create client\n");
	return err;
}

static int __devexit ad525x_remove(struct i2c_client *client)
{
	struct dpot_data *data = i2c_get_clientdata(client);
	struct device *dev = &client->dev;

	switch (data->devid) {
	case AD5258_ID:
	case AD5259_ID:
		sysfs_remove_group(&dev->kobj,
				   &ad525x_group_wipers[AD525X_RDAC0]);
		break;
	case AD5251_ID:
	case AD5252_ID:
		sysfs_remove_group(&dev->kobj,
				   &ad525x_group_wipers[AD525X_RDAC1]);
		sysfs_remove_group(&dev->kobj,
				   &ad525x_group_wipers[AD525X_RDAC3]);
		sysfs_remove_group(&dev->kobj, &ad525x_group_commands);
		break;
	case AD5253_ID:
	case AD5254_ID:
		sysfs_remove_group(&dev->kobj,
				   &ad525x_group_wipers[AD525X_RDAC0]);
		sysfs_remove_group(&dev->kobj,
				   &ad525x_group_wipers[AD525X_RDAC1]);
		sysfs_remove_group(&dev->kobj,
				   &ad525x_group_wipers[AD525X_RDAC2]);
		sysfs_remove_group(&dev->kobj,
				   &ad525x_group_wipers[AD525X_RDAC3]);
		sysfs_remove_group(&dev->kobj, &ad525x_group_commands);
		break;
	case AD5255_ID:
		sysfs_remove_group(&dev->kobj,
				   &ad525x_group_wipers[AD525X_RDAC0]);
		sysfs_remove_group(&dev->kobj,
				   &ad525x_group_wipers[AD525X_RDAC1]);
		sysfs_remove_group(&dev->kobj,
				   &ad525x_group_wipers[AD525X_RDAC2]);
		sysfs_remove_group(&dev->kobj, &ad525x_group_commands);
		break;
	}

	i2c_set_clientdata(client, NULL);
	kfree(data);

	return 0;
}

static const struct i2c_device_id ad525x_idtable[] = {
	{"ad5258", AD5258_ID},
	{"ad5259", AD5259_ID},
	{"ad5251", AD5251_ID},
	{"ad5252", AD5252_ID},
	{"ad5253", AD5253_ID},
	{"ad5254", AD5254_ID},
	{"ad5255", AD5255_ID},
	{}
};

MODULE_DEVICE_TABLE(i2c, ad525x_idtable);

static struct i2c_driver ad525x_driver = {
	.driver = {
		   .owner = THIS_MODULE,
		   .name = DRIVER_NAME,
		   },
	.id_table = ad525x_idtable,
	.probe = ad525x_probe,
	.remove = __devexit_p(ad525x_remove),
};

static int __init ad525x_init(void)
{
	return i2c_add_driver(&ad525x_driver);
}

module_init(ad525x_init);

static void __exit ad525x_exit(void)
{
	i2c_del_driver(&ad525x_driver);
}

module_exit(ad525x_exit);

MODULE_AUTHOR("Chris Verges <chrisv@cyberswitching.com>, "
	      "Michael Hennerich <hennerich@blackfin.uclinux.org>, ");
MODULE_DESCRIPTION("AD5258/9 digital potentiometer driver");
MODULE_LICENSE("GPL");
MODULE_VERSION(DRIVER_VERSION);
