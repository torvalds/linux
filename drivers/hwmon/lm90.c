/*
 * lm90.c - Part of lm_sensors, Linux kernel modules for hardware
 *          monitoring
 * Copyright (C) 2003-2005  Jean Delvare <khali@linux-fr.org>
 *
 * Based on the lm83 driver. The LM90 is a sensor chip made by National
 * Semiconductor. It reports up to two temperatures (its own plus up to
 * one external one) with a 0.125 deg resolution (1 deg for local
 * temperature) and a 3-4 deg accuracy. Complete datasheet can be
 * obtained from National's website at:
 *   http://www.national.com/pf/LM/LM90.html
 *
 * This driver also supports the LM89 and LM99, two other sensor chips
 * made by National Semiconductor. Both have an increased remote
 * temperature measurement accuracy (1 degree), and the LM99
 * additionally shifts remote temperatures (measured and limits) by 16
 * degrees, which allows for higher temperatures measurement. The
 * driver doesn't handle it since it can be done easily in user-space.
 * Complete datasheets can be obtained from National's website at:
 *   http://www.national.com/pf/LM/LM89.html
 *   http://www.national.com/pf/LM/LM99.html
 * Note that there is no way to differentiate between both chips.
 *
 * This driver also supports the LM86, another sensor chip made by
 * National Semiconductor. It is exactly similar to the LM90 except it
 * has a higher accuracy.
 * Complete datasheet can be obtained from National's website at:
 *   http://www.national.com/pf/LM/LM86.html
 *
 * This driver also supports the ADM1032, a sensor chip made by Analog
 * Devices. That chip is similar to the LM90, with a few differences
 * that are not handled by this driver. Complete datasheet can be
 * obtained from Analog's website at:
 *   http://www.analog.com/en/prod/0,2877,ADM1032,00.html
 * Among others, it has a higher accuracy than the LM90, much like the
 * LM86 does.
 *
 * This driver also supports the MAX6657, MAX6658 and MAX6659 sensor
 * chips made by Maxim. These chips are similar to the LM86. Complete
 * datasheet can be obtained at Maxim's website at:
 *   http://www.maxim-ic.com/quick_view2.cfm/qv_pk/2578
 * Note that there is no easy way to differentiate between the three
 * variants. The extra address and features of the MAX6659 are not
 * supported by this driver.
 *
 * This driver also supports the ADT7461 chip from Analog Devices but
 * only in its "compatability mode". If an ADT7461 chip is found but
 * is configured in non-compatible mode (where its temperature
 * register values are decoded differently) it is ignored by this
 * driver. Complete datasheet can be obtained from Analog's website
 * at:
 *   http://www.analog.com/en/prod/0,2877,ADT7461,00.html
 *
 * Since the LM90 was the first chipset supported by this driver, most
 * comments will refer to this chipset, but are actually general and
 * concern all supported chipsets, unless mentioned otherwise.
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
#include <linux/hwmon-sysfs.h>
#include <linux/hwmon.h>
#include <linux/err.h>

/*
 * Addresses to scan
 * Address is fully defined internally and cannot be changed except for
 * MAX6659.
 * LM86, LM89, LM90, LM99, ADM1032, ADM1032-1, ADT7461, MAX6657 and MAX6658
 * have address 0x4c.
 * ADM1032-2, ADT7461-2, LM89-1, and LM99-1 have address 0x4d.
 * MAX6659 can have address 0x4c, 0x4d or 0x4e (unsupported).
 */

static unsigned short normal_i2c[] = { 0x4c, 0x4d, I2C_CLIENT_END };

/*
 * Insmod parameters
 */

I2C_CLIENT_INSMOD_6(lm90, adm1032, lm99, lm86, max6657, adt7461);

/*
 * The LM90 registers
 */

#define LM90_REG_R_MAN_ID		0xFE
#define LM90_REG_R_CHIP_ID		0xFF
#define LM90_REG_R_CONFIG1		0x03
#define LM90_REG_W_CONFIG1		0x09
#define LM90_REG_R_CONFIG2		0xBF
#define LM90_REG_W_CONFIG2		0xBF
#define LM90_REG_R_CONVRATE		0x04
#define LM90_REG_W_CONVRATE		0x0A
#define LM90_REG_R_STATUS		0x02
#define LM90_REG_R_LOCAL_TEMP		0x00
#define LM90_REG_R_LOCAL_HIGH		0x05
#define LM90_REG_W_LOCAL_HIGH		0x0B
#define LM90_REG_R_LOCAL_LOW		0x06
#define LM90_REG_W_LOCAL_LOW		0x0C
#define LM90_REG_R_LOCAL_CRIT		0x20
#define LM90_REG_W_LOCAL_CRIT		0x20
#define LM90_REG_R_REMOTE_TEMPH		0x01
#define LM90_REG_R_REMOTE_TEMPL		0x10
#define LM90_REG_R_REMOTE_OFFSH		0x11
#define LM90_REG_W_REMOTE_OFFSH		0x11
#define LM90_REG_R_REMOTE_OFFSL		0x12
#define LM90_REG_W_REMOTE_OFFSL		0x12
#define LM90_REG_R_REMOTE_HIGHH		0x07
#define LM90_REG_W_REMOTE_HIGHH		0x0D
#define LM90_REG_R_REMOTE_HIGHL		0x13
#define LM90_REG_W_REMOTE_HIGHL		0x13
#define LM90_REG_R_REMOTE_LOWH		0x08
#define LM90_REG_W_REMOTE_LOWH		0x0E
#define LM90_REG_R_REMOTE_LOWL		0x14
#define LM90_REG_W_REMOTE_LOWL		0x14
#define LM90_REG_R_REMOTE_CRIT		0x19
#define LM90_REG_W_REMOTE_CRIT		0x19
#define LM90_REG_R_TCRIT_HYST		0x21
#define LM90_REG_W_TCRIT_HYST		0x21

/*
 * Conversions and various macros
 * For local temperatures and limits, critical limits and the hysteresis
 * value, the LM90 uses signed 8-bit values with LSB = 1 degree Celsius.
 * For remote temperatures and limits, it uses signed 11-bit values with
 * LSB = 0.125 degree Celsius, left-justified in 16-bit registers.
 */

#define TEMP1_FROM_REG(val)	((val) * 1000)
#define TEMP1_TO_REG(val)	((val) <= -128000 ? -128 : \
				 (val) >= 127000 ? 127 : \
				 (val) < 0 ? ((val) - 500) / 1000 : \
				 ((val) + 500) / 1000)
#define TEMP2_FROM_REG(val)	((val) / 32 * 125)
#define TEMP2_TO_REG(val)	((val) <= -128000 ? 0x8000 : \
				 (val) >= 127875 ? 0x7FE0 : \
				 (val) < 0 ? ((val) - 62) / 125 * 32 : \
				 ((val) + 62) / 125 * 32)
#define HYST_TO_REG(val)	((val) <= 0 ? 0 : (val) >= 30500 ? 31 : \
				 ((val) + 500) / 1000)

/* 
 * ADT7461 is almost identical to LM90 except that attempts to write
 * values that are outside the range 0 < temp < 127 are treated as
 * the boundary value. 
 */

#define TEMP1_TO_REG_ADT7461(val) ((val) <= 0 ? 0 : \
				 (val) >= 127000 ? 127 : \
				 ((val) + 500) / 1000)
#define TEMP2_TO_REG_ADT7461(val) ((val) <= 0 ? 0 : \
				 (val) >= 127750 ? 0x7FC0 : \
				 ((val) + 125) / 250 * 64)

/*
 * Functions declaration
 */

static int lm90_attach_adapter(struct i2c_adapter *adapter);
static int lm90_detect(struct i2c_adapter *adapter, int address,
	int kind);
static void lm90_init_client(struct i2c_client *client);
static int lm90_detach_client(struct i2c_client *client);
static struct lm90_data *lm90_update_device(struct device *dev);

/*
 * Driver data (common to all clients)
 */

static struct i2c_driver lm90_driver = {
	.owner		= THIS_MODULE,
	.name		= "lm90",
	.id		= I2C_DRIVERID_LM90,
	.flags		= I2C_DF_NOTIFY,
	.attach_adapter	= lm90_attach_adapter,
	.detach_client	= lm90_detach_client,
};

/*
 * Client data (each client gets its own)
 */

struct lm90_data {
	struct i2c_client client;
	struct class_device *class_dev;
	struct semaphore update_lock;
	char valid; /* zero until following fields are valid */
	unsigned long last_updated; /* in jiffies */
	int kind;

	/* registers values */
	s8 temp8[5];	/* 0: local input
			   1: local low limit
			   2: local high limit
			   3: local critical limit
			   4: remote critical limit */
	s16 temp11[3];	/* 0: remote input
			   1: remote low limit
			   2: remote high limit */
	u8 temp_hyst;
	u8 alarms; /* bitvector */
};

/*
 * Sysfs stuff
 */

static ssize_t show_temp8(struct device *dev, struct device_attribute *devattr,
			  char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct lm90_data *data = lm90_update_device(dev);
	return sprintf(buf, "%d\n", TEMP1_FROM_REG(data->temp8[attr->index]));
}

static ssize_t set_temp8(struct device *dev, struct device_attribute *devattr,
			 const char *buf, size_t count)
{
	static const u8 reg[4] = {
		LM90_REG_W_LOCAL_LOW,
		LM90_REG_W_LOCAL_HIGH,
		LM90_REG_W_LOCAL_CRIT,
		LM90_REG_W_REMOTE_CRIT,
	};

	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct i2c_client *client = to_i2c_client(dev);
	struct lm90_data *data = i2c_get_clientdata(client);
	long val = simple_strtol(buf, NULL, 10);
	int nr = attr->index;

	down(&data->update_lock);
	if (data->kind == adt7461)
		data->temp8[nr] = TEMP1_TO_REG_ADT7461(val);
	else
		data->temp8[nr] = TEMP1_TO_REG(val);
	i2c_smbus_write_byte_data(client, reg[nr - 1], data->temp8[nr]);
	up(&data->update_lock);
	return count;
}

static ssize_t show_temp11(struct device *dev, struct device_attribute *devattr,
			   char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct lm90_data *data = lm90_update_device(dev);
	return sprintf(buf, "%d\n", TEMP2_FROM_REG(data->temp11[attr->index]));
}

static ssize_t set_temp11(struct device *dev, struct device_attribute *devattr,
			  const char *buf, size_t count)
{
	static const u8 reg[4] = {
		LM90_REG_W_REMOTE_LOWH,
		LM90_REG_W_REMOTE_LOWL,
		LM90_REG_W_REMOTE_HIGHH,
		LM90_REG_W_REMOTE_HIGHL,
	};

	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct i2c_client *client = to_i2c_client(dev);
	struct lm90_data *data = i2c_get_clientdata(client);
	long val = simple_strtol(buf, NULL, 10);
	int nr = attr->index;

	down(&data->update_lock);
	if (data->kind == adt7461)
		data->temp11[nr] = TEMP2_TO_REG_ADT7461(val);
	else
		data->temp11[nr] = TEMP2_TO_REG(val);
	i2c_smbus_write_byte_data(client, reg[(nr - 1) * 2],
				  data->temp11[nr] >> 8);
	i2c_smbus_write_byte_data(client, reg[(nr - 1) * 2 + 1],
				  data->temp11[nr] & 0xff);
	up(&data->update_lock);
	return count;
}

static ssize_t show_temphyst(struct device *dev, struct device_attribute *devattr,
			     char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct lm90_data *data = lm90_update_device(dev);
	return sprintf(buf, "%d\n", TEMP1_FROM_REG(data->temp8[attr->index])
		       - TEMP1_FROM_REG(data->temp_hyst));
}

static ssize_t set_temphyst(struct device *dev, struct device_attribute *dummy,
			    const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct lm90_data *data = i2c_get_clientdata(client);
	long val = simple_strtol(buf, NULL, 10);
	long hyst;

	down(&data->update_lock);
	hyst = TEMP1_FROM_REG(data->temp8[3]) - val;
	i2c_smbus_write_byte_data(client, LM90_REG_W_TCRIT_HYST,
				  HYST_TO_REG(hyst));
	up(&data->update_lock);
	return count;
}

static ssize_t show_alarms(struct device *dev, struct device_attribute *dummy,
			   char *buf)
{
	struct lm90_data *data = lm90_update_device(dev);
	return sprintf(buf, "%d\n", data->alarms);
}

static SENSOR_DEVICE_ATTR(temp1_input, S_IRUGO, show_temp8, NULL, 0);
static SENSOR_DEVICE_ATTR(temp2_input, S_IRUGO, show_temp11, NULL, 0);
static SENSOR_DEVICE_ATTR(temp1_min, S_IWUSR | S_IRUGO, show_temp8,
	set_temp8, 1);
static SENSOR_DEVICE_ATTR(temp2_min, S_IWUSR | S_IRUGO, show_temp11,
	set_temp11, 1);
static SENSOR_DEVICE_ATTR(temp1_max, S_IWUSR | S_IRUGO, show_temp8,
	set_temp8, 2);
static SENSOR_DEVICE_ATTR(temp2_max, S_IWUSR | S_IRUGO, show_temp11,
	set_temp11, 2);
static SENSOR_DEVICE_ATTR(temp1_crit, S_IWUSR | S_IRUGO, show_temp8,
	set_temp8, 3);
static SENSOR_DEVICE_ATTR(temp2_crit, S_IWUSR | S_IRUGO, show_temp8,
	set_temp8, 4);
static SENSOR_DEVICE_ATTR(temp1_crit_hyst, S_IWUSR | S_IRUGO, show_temphyst,
	set_temphyst, 3);
static SENSOR_DEVICE_ATTR(temp2_crit_hyst, S_IRUGO, show_temphyst, NULL, 4);
static DEVICE_ATTR(alarms, S_IRUGO, show_alarms, NULL);

/* pec used for ADM1032 only */
static ssize_t show_pec(struct device *dev, struct device_attribute *dummy,
			char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	return sprintf(buf, "%d\n", !!(client->flags & I2C_CLIENT_PEC));
}

static ssize_t set_pec(struct device *dev, struct device_attribute *dummy,
		       const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	long val = simple_strtol(buf, NULL, 10);

	switch (val) {
	case 0:
		client->flags &= ~I2C_CLIENT_PEC;
		break;
	case 1:
		client->flags |= I2C_CLIENT_PEC;
		break;
	default:
		return -EINVAL;
	}

	return count;
}

static DEVICE_ATTR(pec, S_IWUSR | S_IRUGO, show_pec, set_pec);

/*
 * Real code
 */

/* The ADM1032 supports PEC but not on write byte transactions, so we need
   to explicitely ask for a transaction without PEC. */
static inline s32 adm1032_write_byte(struct i2c_client *client, u8 value)
{
	return i2c_smbus_xfer(client->adapter, client->addr,
			      client->flags & ~I2C_CLIENT_PEC,
			      I2C_SMBUS_WRITE, value, I2C_SMBUS_BYTE, NULL);
}

/* It is assumed that client->update_lock is held (unless we are in
   detection or initialization steps). This matters when PEC is enabled,
   because we don't want the address pointer to change between the write
   byte and the read byte transactions. */
static int lm90_read_reg(struct i2c_client* client, u8 reg, u8 *value)
{
	int err;

 	if (client->flags & I2C_CLIENT_PEC) {
 		err = adm1032_write_byte(client, reg);
 		if (err >= 0)
 			err = i2c_smbus_read_byte(client);
 	} else
 		err = i2c_smbus_read_byte_data(client, reg);

	if (err < 0) {
		dev_warn(&client->dev, "Register %#02x read failed (%d)\n",
			 reg, err);
		return err;
	}
	*value = err;

	return 0;
}

static int lm90_attach_adapter(struct i2c_adapter *adapter)
{
	if (!(adapter->class & I2C_CLASS_HWMON))
		return 0;
	return i2c_probe(adapter, &addr_data, lm90_detect);
}

/*
 * The following function does more than just detection. If detection
 * succeeds, it also registers the new chip.
 */
static int lm90_detect(struct i2c_adapter *adapter, int address, int kind)
{
	struct i2c_client *new_client;
	struct lm90_data *data;
	int err = 0;
	const char *name = "";

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA))
		goto exit;

	if (!(data = kzalloc(sizeof(struct lm90_data), GFP_KERNEL))) {
		err = -ENOMEM;
		goto exit;
	}

	/* The common I2C client data is placed right before the
	   LM90-specific data. */
	new_client = &data->client;
	i2c_set_clientdata(new_client, data);
	new_client->addr = address;
	new_client->adapter = adapter;
	new_client->driver = &lm90_driver;
	new_client->flags = 0;

	/*
	 * Now we do the remaining detection. A negative kind means that
	 * the driver was loaded with no force parameter (default), so we
	 * must both detect and identify the chip. A zero kind means that
	 * the driver was loaded with the force parameter, the detection
	 * step shall be skipped. A positive kind means that the driver
	 * was loaded with the force parameter and a given kind of chip is
	 * requested, so both the detection and the identification steps
	 * are skipped.
	 */

	/* Default to an LM90 if forced */
	if (kind == 0)
		kind = lm90;

	if (kind < 0) { /* detection and identification */
		u8 man_id, chip_id, reg_config1, reg_convrate;

		if (lm90_read_reg(new_client, LM90_REG_R_MAN_ID,
				  &man_id) < 0
		 || lm90_read_reg(new_client, LM90_REG_R_CHIP_ID,
		 		  &chip_id) < 0
		 || lm90_read_reg(new_client, LM90_REG_R_CONFIG1,
		 		  &reg_config1) < 0
		 || lm90_read_reg(new_client, LM90_REG_R_CONVRATE,
		 		  &reg_convrate) < 0)
			goto exit_free;
		
		if (man_id == 0x01) { /* National Semiconductor */
			u8 reg_config2;

			if (lm90_read_reg(new_client, LM90_REG_R_CONFIG2,
					  &reg_config2) < 0)
				goto exit_free;

			if ((reg_config1 & 0x2A) == 0x00
			 && (reg_config2 & 0xF8) == 0x00
			 && reg_convrate <= 0x09) {
				if (address == 0x4C
				 && (chip_id & 0xF0) == 0x20) { /* LM90 */
					kind = lm90;
				} else
				if ((chip_id & 0xF0) == 0x30) { /* LM89/LM99 */
					kind = lm99;
				} else
				if (address == 0x4C
				 && (chip_id & 0xF0) == 0x10) { /* LM86 */
					kind = lm86;
				}
			}
		} else
		if (man_id == 0x41) { /* Analog Devices */
			if ((chip_id & 0xF0) == 0x40 /* ADM1032 */
			 && (reg_config1 & 0x3F) == 0x00
			 && reg_convrate <= 0x0A) {
				kind = adm1032;
			} else
			if (chip_id == 0x51 /* ADT7461 */
			 && (reg_config1 & 0x1F) == 0x00 /* check compat mode */
			 && reg_convrate <= 0x0A) {
				kind = adt7461;
			}
		} else
		if (man_id == 0x4D) { /* Maxim */
			/*
			 * The Maxim variants do NOT have a chip_id register.
			 * Reading from that address will return the last read
			 * value, which in our case is those of the man_id
			 * register. Likewise, the config1 register seems to
			 * lack a low nibble, so the value will be those of the
			 * previous read, so in our case those of the man_id
			 * register.
			 */
			if (chip_id == man_id
			 && (reg_config1 & 0x1F) == (man_id & 0x0F)
			 && reg_convrate <= 0x09) {
			 	kind = max6657;
			}
		}

		if (kind <= 0) { /* identification failed */
			dev_info(&adapter->dev,
			    "Unsupported chip (man_id=0x%02X, "
			    "chip_id=0x%02X).\n", man_id, chip_id);
			goto exit_free;
		}
	}

	if (kind == lm90) {
		name = "lm90";
	} else if (kind == adm1032) {
		name = "adm1032";
		/* The ADM1032 supports PEC, but only if combined
		   transactions are not used. */
		if (i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE))
			new_client->flags |= I2C_CLIENT_PEC;
	} else if (kind == lm99) {
		name = "lm99";
	} else if (kind == lm86) {
		name = "lm86";
	} else if (kind == max6657) {
		name = "max6657";
	} else if (kind == adt7461) {
		name = "adt7461";
	}

	/* We can fill in the remaining client fields */
	strlcpy(new_client->name, name, I2C_NAME_SIZE);
	data->valid = 0;
	data->kind = kind;
	init_MUTEX(&data->update_lock);

	/* Tell the I2C layer a new client has arrived */
	if ((err = i2c_attach_client(new_client)))
		goto exit_free;

	/* Initialize the LM90 chip */
	lm90_init_client(new_client);

	/* Register sysfs hooks */
	data->class_dev = hwmon_device_register(&new_client->dev);
	if (IS_ERR(data->class_dev)) {
		err = PTR_ERR(data->class_dev);
		goto exit_detach;
	}

	device_create_file(&new_client->dev,
			   &sensor_dev_attr_temp1_input.dev_attr);
	device_create_file(&new_client->dev,
			   &sensor_dev_attr_temp2_input.dev_attr);
	device_create_file(&new_client->dev,
			   &sensor_dev_attr_temp1_min.dev_attr);
	device_create_file(&new_client->dev,
			   &sensor_dev_attr_temp2_min.dev_attr);
	device_create_file(&new_client->dev,
			   &sensor_dev_attr_temp1_max.dev_attr);
	device_create_file(&new_client->dev,
			   &sensor_dev_attr_temp2_max.dev_attr);
	device_create_file(&new_client->dev,
			   &sensor_dev_attr_temp1_crit.dev_attr);
	device_create_file(&new_client->dev,
			   &sensor_dev_attr_temp2_crit.dev_attr);
	device_create_file(&new_client->dev,
			   &sensor_dev_attr_temp1_crit_hyst.dev_attr);
	device_create_file(&new_client->dev,
			   &sensor_dev_attr_temp2_crit_hyst.dev_attr);
	device_create_file(&new_client->dev, &dev_attr_alarms);

	if (new_client->flags & I2C_CLIENT_PEC)
		device_create_file(&new_client->dev, &dev_attr_pec);

	return 0;

exit_detach:
	i2c_detach_client(new_client);
exit_free:
	kfree(data);
exit:
	return err;
}

static void lm90_init_client(struct i2c_client *client)
{
	u8 config;

	/*
	 * Start the conversions.
	 */
	i2c_smbus_write_byte_data(client, LM90_REG_W_CONVRATE,
				  5); /* 2 Hz */
	if (lm90_read_reg(client, LM90_REG_R_CONFIG1, &config) < 0) {
		dev_warn(&client->dev, "Initialization failed!\n");
		return;
	}
	if (config & 0x40)
		i2c_smbus_write_byte_data(client, LM90_REG_W_CONFIG1,
					  config & 0xBF); /* run */
}

static int lm90_detach_client(struct i2c_client *client)
{
	struct lm90_data *data = i2c_get_clientdata(client);
	int err;

	hwmon_device_unregister(data->class_dev);

	if ((err = i2c_detach_client(client)))
		return err;

	kfree(data);
	return 0;
}

static struct lm90_data *lm90_update_device(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct lm90_data *data = i2c_get_clientdata(client);

	down(&data->update_lock);

	if (time_after(jiffies, data->last_updated + HZ * 2) || !data->valid) {
		u8 oldh, newh, l;

		dev_dbg(&client->dev, "Updating lm90 data.\n");
		lm90_read_reg(client, LM90_REG_R_LOCAL_TEMP, &data->temp8[0]);
		lm90_read_reg(client, LM90_REG_R_LOCAL_LOW, &data->temp8[1]);
		lm90_read_reg(client, LM90_REG_R_LOCAL_HIGH, &data->temp8[2]);
		lm90_read_reg(client, LM90_REG_R_LOCAL_CRIT, &data->temp8[3]);
		lm90_read_reg(client, LM90_REG_R_REMOTE_CRIT, &data->temp8[4]);
		lm90_read_reg(client, LM90_REG_R_TCRIT_HYST, &data->temp_hyst);

		/*
		 * There is a trick here. We have to read two registers to
		 * have the remote sensor temperature, but we have to beware
		 * a conversion could occur inbetween the readings. The
		 * datasheet says we should either use the one-shot
		 * conversion register, which we don't want to do (disables
		 * hardware monitoring) or monitor the busy bit, which is
		 * impossible (we can't read the values and monitor that bit
		 * at the exact same time). So the solution used here is to
		 * read the high byte once, then the low byte, then the high
		 * byte again. If the new high byte matches the old one,
		 * then we have a valid reading. Else we have to read the low
		 * byte again, and now we believe we have a correct reading.
		 */
		if (lm90_read_reg(client, LM90_REG_R_REMOTE_TEMPH, &oldh) == 0
		 && lm90_read_reg(client, LM90_REG_R_REMOTE_TEMPL, &l) == 0
		 && lm90_read_reg(client, LM90_REG_R_REMOTE_TEMPH, &newh) == 0
		 && (newh == oldh
		  || lm90_read_reg(client, LM90_REG_R_REMOTE_TEMPL, &l) == 0))
			data->temp11[0] = (newh << 8) | l;

		if (lm90_read_reg(client, LM90_REG_R_REMOTE_LOWH, &newh) == 0
		 && lm90_read_reg(client, LM90_REG_R_REMOTE_LOWL, &l) == 0)
			data->temp11[1] = (newh << 8) | l;
		if (lm90_read_reg(client, LM90_REG_R_REMOTE_HIGHH, &newh) == 0
		 && lm90_read_reg(client, LM90_REG_R_REMOTE_HIGHL, &l) == 0)
			data->temp11[2] = (newh << 8) | l;
		lm90_read_reg(client, LM90_REG_R_STATUS, &data->alarms);

		data->last_updated = jiffies;
		data->valid = 1;
	}

	up(&data->update_lock);

	return data;
}

static int __init sensors_lm90_init(void)
{
	return i2c_add_driver(&lm90_driver);
}

static void __exit sensors_lm90_exit(void)
{
	i2c_del_driver(&lm90_driver);
}

MODULE_AUTHOR("Jean Delvare <khali@linux-fr.org>");
MODULE_DESCRIPTION("LM90/ADM1032 driver");
MODULE_LICENSE("GPL");

module_init(sensors_lm90_init);
module_exit(sensors_lm90_exit);
