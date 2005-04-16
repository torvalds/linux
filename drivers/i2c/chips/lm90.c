/*
 * lm90.c - Part of lm_sensors, Linux kernel modules for hardware
 *          monitoring
 * Copyright (C) 2003-2004  Jean Delvare <khali@linux-fr.org>
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
 * Note that there is no way to differenciate between both chips.
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
 *   http://products.analog.com/products/info.asp?product=ADM1032
 * Among others, it has a higher accuracy than the LM90, much like the
 * LM86 does.
 *
 * This driver also supports the MAX6657, MAX6658 and MAX6659 sensor
 * chips made by Maxim. These chips are similar to the LM86. Complete
 * datasheet can be obtained at Maxim's website at:
 *   http://www.maxim-ic.com/quick_view2.cfm/qv_pk/2578
 * Note that there is no easy way to differenciate between the three
 * variants. The extra address and features of the MAX6659 are not
 * supported by this driver.
 *
 * This driver also supports the ADT7461 chip from Analog Devices but
 * only in its "compatability mode". If an ADT7461 chip is found but
 * is configured in non-compatible mode (where its temperature
 * register values are decoded differently) it is ignored by this
 * driver. Complete datasheet can be obtained from Analog's website
 * at:
 *   http://products.analog.com/products/info.asp?product=ADT7461
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

#include <linux/config.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/jiffies.h>
#include <linux/i2c.h>
#include <linux/i2c-sensor.h>

/*
 * Addresses to scan
 * Address is fully defined internally and cannot be changed except for
 * MAX6659.
 * LM86, LM89, LM90, LM99, ADM1032, MAX6657 and MAX6658 have address 0x4c.
 * LM89-1, and LM99-1 have address 0x4d.
 * MAX6659 can have address 0x4c, 0x4d or 0x4e (unsupported).
 * ADT7461 always has address 0x4c.
 */

static unsigned short normal_i2c[] = { 0x4c, 0x4d, I2C_CLIENT_END };
static unsigned int normal_isa[] = { I2C_CLIENT_ISA_END };

/*
 * Insmod parameters
 */

SENSORS_INSMOD_6(lm90, adm1032, lm99, lm86, max6657, adt7461);

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
 * value, the LM90 uses signed 8-bit values with LSB = 1 degree Celcius.
 * For remote temperatures and limits, it uses signed 11-bit values with
 * LSB = 0.125 degree Celcius, left-justified in 16-bit registers.
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
	struct semaphore update_lock;
	char valid; /* zero until following fields are valid */
	unsigned long last_updated; /* in jiffies */
	int kind;

	/* registers values */
	s8 temp_input1, temp_low1, temp_high1; /* local */
	s16 temp_input2, temp_low2, temp_high2; /* remote, combined */
	s8 temp_crit1, temp_crit2;
	u8 temp_hyst;
	u8 alarms; /* bitvector */
};

/*
 * Sysfs stuff
 */

#define show_temp(value, converter) \
static ssize_t show_##value(struct device *dev, char *buf) \
{ \
	struct lm90_data *data = lm90_update_device(dev); \
	return sprintf(buf, "%d\n", converter(data->value)); \
}
show_temp(temp_input1, TEMP1_FROM_REG);
show_temp(temp_input2, TEMP2_FROM_REG);
show_temp(temp_low1, TEMP1_FROM_REG);
show_temp(temp_low2, TEMP2_FROM_REG);
show_temp(temp_high1, TEMP1_FROM_REG);
show_temp(temp_high2, TEMP2_FROM_REG);
show_temp(temp_crit1, TEMP1_FROM_REG);
show_temp(temp_crit2, TEMP1_FROM_REG);

#define set_temp1(value, reg) \
static ssize_t set_##value(struct device *dev, const char *buf, \
	size_t count) \
{ \
	struct i2c_client *client = to_i2c_client(dev); \
	struct lm90_data *data = i2c_get_clientdata(client); \
	long val = simple_strtol(buf, NULL, 10); \
 \
	down(&data->update_lock); \
	if (data->kind == adt7461) \
		data->value = TEMP1_TO_REG_ADT7461(val); \
	else \
		data->value = TEMP1_TO_REG(val); \
	i2c_smbus_write_byte_data(client, reg, data->value); \
	up(&data->update_lock); \
	return count; \
}
#define set_temp2(value, regh, regl) \
static ssize_t set_##value(struct device *dev, const char *buf, \
	size_t count) \
{ \
	struct i2c_client *client = to_i2c_client(dev); \
	struct lm90_data *data = i2c_get_clientdata(client); \
	long val = simple_strtol(buf, NULL, 10); \
 \
	down(&data->update_lock); \
	if (data->kind == adt7461) \
		data->value = TEMP2_TO_REG_ADT7461(val); \
	else \
		data->value = TEMP2_TO_REG(val); \
	i2c_smbus_write_byte_data(client, regh, data->value >> 8); \
	i2c_smbus_write_byte_data(client, regl, data->value & 0xff); \
	up(&data->update_lock); \
	return count; \
}
set_temp1(temp_low1, LM90_REG_W_LOCAL_LOW);
set_temp2(temp_low2, LM90_REG_W_REMOTE_LOWH, LM90_REG_W_REMOTE_LOWL);
set_temp1(temp_high1, LM90_REG_W_LOCAL_HIGH);
set_temp2(temp_high2, LM90_REG_W_REMOTE_HIGHH, LM90_REG_W_REMOTE_HIGHL);
set_temp1(temp_crit1, LM90_REG_W_LOCAL_CRIT);
set_temp1(temp_crit2, LM90_REG_W_REMOTE_CRIT);

#define show_temp_hyst(value, basereg) \
static ssize_t show_##value(struct device *dev, char *buf) \
{ \
	struct lm90_data *data = lm90_update_device(dev); \
	return sprintf(buf, "%d\n", TEMP1_FROM_REG(data->basereg) \
		       - TEMP1_FROM_REG(data->temp_hyst)); \
}
show_temp_hyst(temp_hyst1, temp_crit1);
show_temp_hyst(temp_hyst2, temp_crit2);

static ssize_t set_temp_hyst1(struct device *dev, const char *buf,
	size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct lm90_data *data = i2c_get_clientdata(client);
	long val = simple_strtol(buf, NULL, 10);
	long hyst;

	down(&data->update_lock);
	hyst = TEMP1_FROM_REG(data->temp_crit1) - val;
	i2c_smbus_write_byte_data(client, LM90_REG_W_TCRIT_HYST,
				  HYST_TO_REG(hyst));
	up(&data->update_lock);
	return count;
}

static ssize_t show_alarms(struct device *dev, char *buf)
{
	struct lm90_data *data = lm90_update_device(dev);
	return sprintf(buf, "%d\n", data->alarms);
}

static DEVICE_ATTR(temp1_input, S_IRUGO, show_temp_input1, NULL);
static DEVICE_ATTR(temp2_input, S_IRUGO, show_temp_input2, NULL);
static DEVICE_ATTR(temp1_min, S_IWUSR | S_IRUGO, show_temp_low1,
	set_temp_low1);
static DEVICE_ATTR(temp2_min, S_IWUSR | S_IRUGO, show_temp_low2,
	set_temp_low2);
static DEVICE_ATTR(temp1_max, S_IWUSR | S_IRUGO, show_temp_high1,
	set_temp_high1);
static DEVICE_ATTR(temp2_max, S_IWUSR | S_IRUGO, show_temp_high2,
	set_temp_high2);
static DEVICE_ATTR(temp1_crit, S_IWUSR | S_IRUGO, show_temp_crit1,
	set_temp_crit1);
static DEVICE_ATTR(temp2_crit, S_IWUSR | S_IRUGO, show_temp_crit2,
	set_temp_crit2);
static DEVICE_ATTR(temp1_crit_hyst, S_IWUSR | S_IRUGO, show_temp_hyst1,
	set_temp_hyst1);
static DEVICE_ATTR(temp2_crit_hyst, S_IRUGO, show_temp_hyst2, NULL);
static DEVICE_ATTR(alarms, S_IRUGO, show_alarms, NULL);

/*
 * Real code
 */

static int lm90_attach_adapter(struct i2c_adapter *adapter)
{
	if (!(adapter->class & I2C_CLASS_HWMON))
		return 0;
	return i2c_detect(adapter, &addr_data, lm90_detect);
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

	if (!(data = kmalloc(sizeof(struct lm90_data), GFP_KERNEL))) {
		err = -ENOMEM;
		goto exit;
	}
	memset(data, 0, sizeof(struct lm90_data));

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

		man_id = i2c_smbus_read_byte_data(new_client,
			 LM90_REG_R_MAN_ID);
		chip_id = i2c_smbus_read_byte_data(new_client,
			  LM90_REG_R_CHIP_ID);
		reg_config1 = i2c_smbus_read_byte_data(new_client,
			      LM90_REG_R_CONFIG1);
		reg_convrate = i2c_smbus_read_byte_data(new_client,
			       LM90_REG_R_CONVRATE);
		
		if (man_id == 0x01) { /* National Semiconductor */
			u8 reg_config2;

			reg_config2 = i2c_smbus_read_byte_data(new_client,
				      LM90_REG_R_CONFIG2);

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
			if (address == 0x4C
			 && (chip_id & 0xF0) == 0x40 /* ADM1032 */
			 && (reg_config1 & 0x3F) == 0x00
			 && reg_convrate <= 0x0A) {
				kind = adm1032;
			} else
			if (address == 0x4c
			 && chip_id == 0x51 /* ADT7461 */
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
	device_create_file(&new_client->dev, &dev_attr_temp1_input);
	device_create_file(&new_client->dev, &dev_attr_temp2_input);
	device_create_file(&new_client->dev, &dev_attr_temp1_min);
	device_create_file(&new_client->dev, &dev_attr_temp2_min);
	device_create_file(&new_client->dev, &dev_attr_temp1_max);
	device_create_file(&new_client->dev, &dev_attr_temp2_max);
	device_create_file(&new_client->dev, &dev_attr_temp1_crit);
	device_create_file(&new_client->dev, &dev_attr_temp2_crit);
	device_create_file(&new_client->dev, &dev_attr_temp1_crit_hyst);
	device_create_file(&new_client->dev, &dev_attr_temp2_crit_hyst);
	device_create_file(&new_client->dev, &dev_attr_alarms);

	return 0;

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
	config = i2c_smbus_read_byte_data(client, LM90_REG_R_CONFIG1);
	if (config & 0x40)
		i2c_smbus_write_byte_data(client, LM90_REG_W_CONFIG1,
					  config & 0xBF); /* run */
}

static int lm90_detach_client(struct i2c_client *client)
{
	int err;

	if ((err = i2c_detach_client(client))) {
		dev_err(&client->dev, "Client deregistration failed, "
			"client not detached.\n");
		return err;
	}

	kfree(i2c_get_clientdata(client));
	return 0;
}

static struct lm90_data *lm90_update_device(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct lm90_data *data = i2c_get_clientdata(client);

	down(&data->update_lock);

	if (time_after(jiffies, data->last_updated + HZ * 2) || !data->valid) {
		u8 oldh, newh;

		dev_dbg(&client->dev, "Updating lm90 data.\n");
		data->temp_input1 = i2c_smbus_read_byte_data(client,
				    LM90_REG_R_LOCAL_TEMP);
		data->temp_high1 = i2c_smbus_read_byte_data(client,
				   LM90_REG_R_LOCAL_HIGH);
		data->temp_low1 = i2c_smbus_read_byte_data(client,
				  LM90_REG_R_LOCAL_LOW);
		data->temp_crit1 = i2c_smbus_read_byte_data(client,
				   LM90_REG_R_LOCAL_CRIT);
		data->temp_crit2 = i2c_smbus_read_byte_data(client,
				   LM90_REG_R_REMOTE_CRIT);
		data->temp_hyst = i2c_smbus_read_byte_data(client,
				  LM90_REG_R_TCRIT_HYST);

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
		oldh = i2c_smbus_read_byte_data(client,
		       LM90_REG_R_REMOTE_TEMPH);
		data->temp_input2 = i2c_smbus_read_byte_data(client,
				    LM90_REG_R_REMOTE_TEMPL);
		newh = i2c_smbus_read_byte_data(client,
		       LM90_REG_R_REMOTE_TEMPH);
		if (newh != oldh) {
			data->temp_input2 = i2c_smbus_read_byte_data(client,
					    LM90_REG_R_REMOTE_TEMPL);
#ifdef DEBUG
			oldh = i2c_smbus_read_byte_data(client,
			       LM90_REG_R_REMOTE_TEMPH);
			/* oldh is actually newer */
			if (newh != oldh)
				dev_warn(&client->dev, "Remote temperature may be "
					 "wrong.\n");
#endif
		}
		data->temp_input2 |= (newh << 8);

		data->temp_high2 = (i2c_smbus_read_byte_data(client,
				   LM90_REG_R_REMOTE_HIGHH) << 8) +
				   i2c_smbus_read_byte_data(client,
				   LM90_REG_R_REMOTE_HIGHL);
		data->temp_low2 = (i2c_smbus_read_byte_data(client,
				  LM90_REG_R_REMOTE_LOWH) << 8) +
				  i2c_smbus_read_byte_data(client,
				  LM90_REG_R_REMOTE_LOWL);
		data->alarms = i2c_smbus_read_byte_data(client,
			       LM90_REG_R_STATUS);

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
