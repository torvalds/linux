/*
    smsc47m1.c - Part of lm_sensors, Linux kernel modules
                 for hardware monitoring

    Supports the SMSC LPC47B27x, LPC47M10x, LPC47M13x, LPC47M14x,
    LPC47M15x, LPC47M192 and LPC47M997 Super-I/O chips.

    Copyright (C) 2002 Mark D. Studebaker <mdsxyz123@yahoo.com>
    Copyright (C) 2004 Jean Delvare <khali@linux-fr.org>
    Ported to Linux 2.6 by Gabriele Gorla <gorlik@yahoo.com>
                        and Jean Delvare

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/ioport.h>
#include <linux/jiffies.h>
#include <linux/i2c.h>
#include <linux/i2c-isa.h>
#include <linux/hwmon.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/mutex.h>
#include <asm/io.h>

/* Address is autodetected, there is no default value */
static unsigned short address;

/* Super-I/0 registers and commands */

#define	REG	0x2e	/* The register to read/write */
#define	VAL	0x2f	/* The value to read/write */

static inline void
superio_outb(int reg, int val)
{
	outb(reg, REG);
	outb(val, VAL);
}

static inline int
superio_inb(int reg)
{
	outb(reg, REG);
	return inb(VAL);
}

/* logical device for fans is 0x0A */
#define superio_select() superio_outb(0x07, 0x0A)

static inline void
superio_enter(void)
{
	outb(0x55, REG);
}

static inline void
superio_exit(void)
{
	outb(0xAA, REG);
}

#define SUPERIO_REG_ACT		0x30
#define SUPERIO_REG_BASE	0x60
#define SUPERIO_REG_DEVID	0x20

/* Logical device registers */

#define SMSC_EXTENT		0x80

/* nr is 0 or 1 in the macros below */
#define SMSC47M1_REG_ALARM		0x04
#define SMSC47M1_REG_TPIN(nr)		(0x34 - (nr))
#define SMSC47M1_REG_PPIN(nr)		(0x36 - (nr))
#define SMSC47M1_REG_PWM(nr)		(0x56 + (nr))
#define SMSC47M1_REG_FANDIV		0x58
#define SMSC47M1_REG_FAN(nr)		(0x59 + (nr))
#define SMSC47M1_REG_FAN_PRELOAD(nr)	(0x5B + (nr))

#define MIN_FROM_REG(reg,div)		((reg)>=192 ? 0 : \
					 983040/((192-(reg))*(div)))
#define FAN_FROM_REG(reg,div,preload)	((reg)<=(preload) || (reg)==255 ? 0 : \
					 983040/(((reg)-(preload))*(div)))
#define DIV_FROM_REG(reg)		(1 << (reg))
#define PWM_FROM_REG(reg)		(((reg) & 0x7E) << 1)
#define PWM_EN_FROM_REG(reg)		((~(reg)) & 0x01)
#define PWM_TO_REG(reg)			(((reg) >> 1) & 0x7E)

struct smsc47m1_data {
	struct i2c_client client;
	struct class_device *class_dev;
	struct mutex lock;

	struct mutex update_lock;
	unsigned long last_updated;	/* In jiffies */

	u8 fan[2];		/* Register value */
	u8 fan_preload[2];	/* Register value */
	u8 fan_div[2];		/* Register encoding, shifted right */
	u8 alarms;		/* Register encoding */
	u8 pwm[2];		/* Register value (bit 7 is enable) */
};


static int smsc47m1_detect(struct i2c_adapter *adapter);
static int smsc47m1_detach_client(struct i2c_client *client);

static int smsc47m1_read_value(struct i2c_client *client, u8 reg);
static void smsc47m1_write_value(struct i2c_client *client, u8 reg, u8 value);

static struct smsc47m1_data *smsc47m1_update_device(struct device *dev,
		int init);


static struct i2c_driver smsc47m1_driver = {
	.driver = {
		.name	= "smsc47m1",
	},
	.attach_adapter	= smsc47m1_detect,
	.detach_client	= smsc47m1_detach_client,
};

/* nr is 0 or 1 in the callback functions below */

static ssize_t get_fan(struct device *dev, char *buf, int nr)
{
	struct smsc47m1_data *data = smsc47m1_update_device(dev, 0);
	/* This chip (stupidly) stops monitoring fan speed if PWM is
	   enabled and duty cycle is 0%. This is fine if the monitoring
	   and control concern the same fan, but troublesome if they are
	   not (which could as well happen). */
	int rpm = (data->pwm[nr] & 0x7F) == 0x00 ? 0 :
		  FAN_FROM_REG(data->fan[nr],
			       DIV_FROM_REG(data->fan_div[nr]),
			       data->fan_preload[nr]);
	return sprintf(buf, "%d\n", rpm);
}

static ssize_t get_fan_min(struct device *dev, char *buf, int nr)
{
	struct smsc47m1_data *data = smsc47m1_update_device(dev, 0);
	int rpm = MIN_FROM_REG(data->fan_preload[nr],
			       DIV_FROM_REG(data->fan_div[nr]));
	return sprintf(buf, "%d\n", rpm);
}

static ssize_t get_fan_div(struct device *dev, char *buf, int nr)
{
	struct smsc47m1_data *data = smsc47m1_update_device(dev, 0);
	return sprintf(buf, "%d\n", DIV_FROM_REG(data->fan_div[nr]));
}

static ssize_t get_pwm(struct device *dev, char *buf, int nr)
{
	struct smsc47m1_data *data = smsc47m1_update_device(dev, 0);
	return sprintf(buf, "%d\n", PWM_FROM_REG(data->pwm[nr]));
}

static ssize_t get_pwm_en(struct device *dev, char *buf, int nr)
{
	struct smsc47m1_data *data = smsc47m1_update_device(dev, 0);
	return sprintf(buf, "%d\n", PWM_EN_FROM_REG(data->pwm[nr]));
}

static ssize_t get_alarms(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct smsc47m1_data *data = smsc47m1_update_device(dev, 0);
	return sprintf(buf, "%d\n", data->alarms);
}

static ssize_t set_fan_min(struct device *dev, const char *buf,
		size_t count, int nr)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct smsc47m1_data *data = i2c_get_clientdata(client);
	long rpmdiv, val = simple_strtol(buf, NULL, 10);

	mutex_lock(&data->update_lock);
	rpmdiv = val * DIV_FROM_REG(data->fan_div[nr]);

	if (983040 > 192 * rpmdiv || 2 * rpmdiv > 983040) {
		mutex_unlock(&data->update_lock);
		return -EINVAL;
	}

	data->fan_preload[nr] = 192 - ((983040 + rpmdiv / 2) / rpmdiv);
	smsc47m1_write_value(client, SMSC47M1_REG_FAN_PRELOAD(nr),
			     data->fan_preload[nr]);
	mutex_unlock(&data->update_lock);

	return count;
}

/* Note: we save and restore the fan minimum here, because its value is
   determined in part by the fan clock divider.  This follows the principle
   of least suprise; the user doesn't expect the fan minimum to change just
   because the divider changed. */
static ssize_t set_fan_div(struct device *dev, const char *buf,
		size_t count, int nr)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct smsc47m1_data *data = i2c_get_clientdata(client);

	long new_div = simple_strtol(buf, NULL, 10), tmp;
	u8 old_div = DIV_FROM_REG(data->fan_div[nr]);

	if (new_div == old_div) /* No change */
		return count;

	mutex_lock(&data->update_lock);
	switch (new_div) {
	case 1: data->fan_div[nr] = 0; break;
	case 2: data->fan_div[nr] = 1; break;
	case 4: data->fan_div[nr] = 2; break;
	case 8: data->fan_div[nr] = 3; break;
	default:
		mutex_unlock(&data->update_lock);
		return -EINVAL;
	}

	tmp = smsc47m1_read_value(client, SMSC47M1_REG_FANDIV) & 0x0F;
	tmp |= (data->fan_div[0] << 4) | (data->fan_div[1] << 6);
	smsc47m1_write_value(client, SMSC47M1_REG_FANDIV, tmp);

	/* Preserve fan min */
	tmp = 192 - (old_div * (192 - data->fan_preload[nr])
		     + new_div / 2) / new_div;
	data->fan_preload[nr] = SENSORS_LIMIT(tmp, 0, 191);
	smsc47m1_write_value(client, SMSC47M1_REG_FAN_PRELOAD(nr),
			     data->fan_preload[nr]);
	mutex_unlock(&data->update_lock);

	return count;
}

static ssize_t set_pwm(struct device *dev, const char *buf,
		size_t count, int nr)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct smsc47m1_data *data = i2c_get_clientdata(client);

	long val = simple_strtol(buf, NULL, 10);

	if (val < 0 || val > 255)
		return -EINVAL;

	mutex_lock(&data->update_lock);
	data->pwm[nr] &= 0x81; /* Preserve additional bits */
	data->pwm[nr] |= PWM_TO_REG(val);
	smsc47m1_write_value(client, SMSC47M1_REG_PWM(nr),
			     data->pwm[nr]);
	mutex_unlock(&data->update_lock);

	return count;
}

static ssize_t set_pwm_en(struct device *dev, const char *buf,
		size_t count, int nr)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct smsc47m1_data *data = i2c_get_clientdata(client);

	long val = simple_strtol(buf, NULL, 10);
	
	if (val != 0 && val != 1)
		return -EINVAL;

	mutex_lock(&data->update_lock);
	data->pwm[nr] &= 0xFE; /* preserve the other bits */
	data->pwm[nr] |= !val;
	smsc47m1_write_value(client, SMSC47M1_REG_PWM(nr),
			     data->pwm[nr]);
	mutex_unlock(&data->update_lock);

	return count;
}

#define fan_present(offset)						\
static ssize_t get_fan##offset (struct device *dev, struct device_attribute *attr, char *buf)		\
{									\
	return get_fan(dev, buf, offset - 1);				\
}									\
static ssize_t get_fan##offset##_min (struct device *dev, struct device_attribute *attr, char *buf)	\
{									\
	return get_fan_min(dev, buf, offset - 1);			\
}									\
static ssize_t set_fan##offset##_min (struct device *dev, struct device_attribute *attr,		\
		const char *buf, size_t count)				\
{									\
	return set_fan_min(dev, buf, count, offset - 1);		\
}									\
static ssize_t get_fan##offset##_div (struct device *dev, struct device_attribute *attr, char *buf)	\
{									\
	return get_fan_div(dev, buf, offset - 1);			\
}									\
static ssize_t set_fan##offset##_div (struct device *dev, struct device_attribute *attr,		\
		const char *buf, size_t count)				\
{									\
	return set_fan_div(dev, buf, count, offset - 1);		\
}									\
static ssize_t get_pwm##offset (struct device *dev, struct device_attribute *attr, char *buf)		\
{									\
	return get_pwm(dev, buf, offset - 1);				\
}									\
static ssize_t set_pwm##offset (struct device *dev, struct device_attribute *attr,			\
		const char *buf, size_t count)				\
{									\
	return set_pwm(dev, buf, count, offset - 1);			\
}									\
static ssize_t get_pwm##offset##_en (struct device *dev, struct device_attribute *attr, char *buf)	\
{									\
	return get_pwm_en(dev, buf, offset - 1);			\
}									\
static ssize_t set_pwm##offset##_en (struct device *dev, struct device_attribute *attr,		\
		const char *buf, size_t count)				\
{									\
	return set_pwm_en(dev, buf, count, offset - 1);			\
}									\
static DEVICE_ATTR(fan##offset##_input, S_IRUGO, get_fan##offset,	\
		NULL);							\
static DEVICE_ATTR(fan##offset##_min, S_IRUGO | S_IWUSR,		\
		get_fan##offset##_min, set_fan##offset##_min);		\
static DEVICE_ATTR(fan##offset##_div, S_IRUGO | S_IWUSR,		\
		get_fan##offset##_div, set_fan##offset##_div);		\
static DEVICE_ATTR(pwm##offset, S_IRUGO | S_IWUSR,			\
		get_pwm##offset, set_pwm##offset);			\
static DEVICE_ATTR(pwm##offset##_enable, S_IRUGO | S_IWUSR,		\
		get_pwm##offset##_en, set_pwm##offset##_en);

fan_present(1);
fan_present(2);

static DEVICE_ATTR(alarms, S_IRUGO, get_alarms, NULL);

static int __init smsc47m1_find(unsigned short *addr)
{
	u8 val;

	superio_enter();
	val = superio_inb(SUPERIO_REG_DEVID);

	/*
	 * SMSC LPC47M10x/LPC47M13x (device id 0x59), LPC47M14x (device id
	 * 0x5F) and LPC47B27x (device id 0x51) have fan control.
	 * The LPC47M15x and LPC47M192 chips "with hardware monitoring block"
	 * can do much more besides (device id 0x60).
	 * The LPC47M997 is undocumented, but seems to be compatible with
	 * the LPC47M192, and has the same device id.
	 */
	if (val == 0x51)
		printk(KERN_INFO "smsc47m1: Found SMSC LPC47B27x\n");
	else if (val == 0x59)
		printk(KERN_INFO "smsc47m1: Found SMSC LPC47M10x/LPC47M13x\n");
	else if (val == 0x5F)
		printk(KERN_INFO "smsc47m1: Found SMSC LPC47M14x\n");
	else if (val == 0x60)
		printk(KERN_INFO "smsc47m1: Found SMSC "
		       "LPC47M15x/LPC47M192/LPC47M997\n");
	else {
		superio_exit();
		return -ENODEV;
	}

	superio_select();
	*addr = (superio_inb(SUPERIO_REG_BASE) << 8)
	      |  superio_inb(SUPERIO_REG_BASE + 1);
	val = superio_inb(SUPERIO_REG_ACT);
	if (*addr == 0 || (val & 0x01) == 0) {
		printk(KERN_INFO "smsc47m1: Device is disabled, will not use\n");
		superio_exit();
		return -ENODEV;
	}

	superio_exit();
	return 0;
}

static int smsc47m1_detect(struct i2c_adapter *adapter)
{
	struct i2c_client *new_client;
	struct smsc47m1_data *data;
	int err = 0;
	int fan1, fan2, pwm1, pwm2;

	if (!request_region(address, SMSC_EXTENT, smsc47m1_driver.driver.name)) {
		dev_err(&adapter->dev, "Region 0x%x already in use!\n", address);
		return -EBUSY;
	}

	if (!(data = kzalloc(sizeof(struct smsc47m1_data), GFP_KERNEL))) {
		err = -ENOMEM;
		goto error_release;
	}

	new_client = &data->client;
	i2c_set_clientdata(new_client, data);
	new_client->addr = address;
	mutex_init(&data->lock);
	new_client->adapter = adapter;
	new_client->driver = &smsc47m1_driver;
	new_client->flags = 0;

	strlcpy(new_client->name, "smsc47m1", I2C_NAME_SIZE);
	mutex_init(&data->update_lock);

	/* If no function is properly configured, there's no point in
	   actually registering the chip. */
	fan1 = (smsc47m1_read_value(new_client, SMSC47M1_REG_TPIN(0)) & 0x05)
	       == 0x05;
	fan2 = (smsc47m1_read_value(new_client, SMSC47M1_REG_TPIN(1)) & 0x05)
	       == 0x05;
	pwm1 = (smsc47m1_read_value(new_client, SMSC47M1_REG_PPIN(0)) & 0x05)
	       == 0x04;
	pwm2 = (smsc47m1_read_value(new_client, SMSC47M1_REG_PPIN(1)) & 0x05)
	       == 0x04;
	if (!(fan1 || fan2 || pwm1 || pwm2)) {
		dev_warn(&new_client->dev, "Device is not configured, will not use\n");
		err = -ENODEV;
		goto error_free;
	}

	if ((err = i2c_attach_client(new_client)))
		goto error_free;

	/* Some values (fan min, clock dividers, pwm registers) may be
	   needed before any update is triggered, so we better read them
	   at least once here. We don't usually do it that way, but in
	   this particular case, manually reading 5 registers out of 8
	   doesn't make much sense and we're better using the existing
	   function. */
	smsc47m1_update_device(&new_client->dev, 1);

	/* Register sysfs hooks */
	data->class_dev = hwmon_device_register(&new_client->dev);
	if (IS_ERR(data->class_dev)) {
		err = PTR_ERR(data->class_dev);
		goto error_detach;
	}

	if (fan1) {
		device_create_file(&new_client->dev, &dev_attr_fan1_input);
		device_create_file(&new_client->dev, &dev_attr_fan1_min);
		device_create_file(&new_client->dev, &dev_attr_fan1_div);
	} else
		dev_dbg(&new_client->dev, "Fan 1 not enabled by hardware, "
			"skipping\n");

	if (fan2) {
		device_create_file(&new_client->dev, &dev_attr_fan2_input);
		device_create_file(&new_client->dev, &dev_attr_fan2_min);
		device_create_file(&new_client->dev, &dev_attr_fan2_div);
	} else
		dev_dbg(&new_client->dev, "Fan 2 not enabled by hardware, "
			"skipping\n");

	if (pwm1) {
		device_create_file(&new_client->dev, &dev_attr_pwm1);
		device_create_file(&new_client->dev, &dev_attr_pwm1_enable);
	} else
		dev_dbg(&new_client->dev, "PWM 1 not enabled by hardware, "
			"skipping\n");
	if (pwm2) {
		device_create_file(&new_client->dev, &dev_attr_pwm2);
		device_create_file(&new_client->dev, &dev_attr_pwm2_enable);
	} else
		dev_dbg(&new_client->dev, "PWM 2 not enabled by hardware, "
			"skipping\n");

	device_create_file(&new_client->dev, &dev_attr_alarms);

	return 0;

error_detach:
	i2c_detach_client(new_client);
error_free:
	kfree(data);
error_release:
	release_region(address, SMSC_EXTENT);
	return err;
}

static int smsc47m1_detach_client(struct i2c_client *client)
{
	struct smsc47m1_data *data = i2c_get_clientdata(client);
	int err;

	hwmon_device_unregister(data->class_dev);

	if ((err = i2c_detach_client(client)))
		return err;

	release_region(client->addr, SMSC_EXTENT);
	kfree(data);

	return 0;
}

static int smsc47m1_read_value(struct i2c_client *client, u8 reg)
{
	int res;

	mutex_lock(&((struct smsc47m1_data *) i2c_get_clientdata(client))->lock);
	res = inb_p(client->addr + reg);
	mutex_unlock(&((struct smsc47m1_data *) i2c_get_clientdata(client))->lock);
	return res;
}

static void smsc47m1_write_value(struct i2c_client *client, u8 reg, u8 value)
{
	mutex_lock(&((struct smsc47m1_data *) i2c_get_clientdata(client))->lock);
	outb_p(value, client->addr + reg);
	mutex_unlock(&((struct smsc47m1_data *) i2c_get_clientdata(client))->lock);
}

static struct smsc47m1_data *smsc47m1_update_device(struct device *dev,
		int init)
{
 	struct i2c_client *client = to_i2c_client(dev);
	struct smsc47m1_data *data = i2c_get_clientdata(client);

	mutex_lock(&data->update_lock);

	if (time_after(jiffies, data->last_updated + HZ + HZ / 2) || init) {
		int i;

		for (i = 0; i < 2; i++) {
			data->fan[i] = smsc47m1_read_value(client,
				       SMSC47M1_REG_FAN(i));
			data->fan_preload[i] = smsc47m1_read_value(client,
					       SMSC47M1_REG_FAN_PRELOAD(i));
			data->pwm[i] = smsc47m1_read_value(client,
				       SMSC47M1_REG_PWM(i));
		}

		i = smsc47m1_read_value(client, SMSC47M1_REG_FANDIV);
		data->fan_div[0] = (i >> 4) & 0x03;
		data->fan_div[1] = i >> 6;

		data->alarms = smsc47m1_read_value(client,
			       SMSC47M1_REG_ALARM) >> 6;
		/* Clear alarms if needed */
		if (data->alarms)
			smsc47m1_write_value(client, SMSC47M1_REG_ALARM, 0xC0);

		data->last_updated = jiffies;
	}

	mutex_unlock(&data->update_lock);
	return data;
}

static int __init sm_smsc47m1_init(void)
{
	if (smsc47m1_find(&address)) {
		return -ENODEV;
	}

	return i2c_isa_add_driver(&smsc47m1_driver);
}

static void __exit sm_smsc47m1_exit(void)
{
	i2c_isa_del_driver(&smsc47m1_driver);
}

MODULE_AUTHOR("Mark D. Studebaker <mdsxyz123@yahoo.com>");
MODULE_DESCRIPTION("SMSC LPC47M1xx fan sensors driver");
MODULE_LICENSE("GPL");

module_init(sm_smsc47m1_init);
module_exit(sm_smsc47m1_exit);
