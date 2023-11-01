// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * f71805f.c - driver for the Fintek F71805F/FG and F71872F/FG Super-I/O
 *             chips integrated hardware monitoring features
 * Copyright (C) 2005-2006  Jean Delvare <jdelvare@suse.de>
 *
 * The F71805F/FG is a LPC Super-I/O chip made by Fintek. It integrates
 * complete hardware monitoring features: voltage, fan and temperature
 * sensors, and manual and automatic fan speed control.
 *
 * The F71872F/FG is almost the same, with two more voltages monitored,
 * and 6 VID inputs.
 *
 * The F71806F/FG is essentially the same as the F71872F/FG. It even has
 * the same chip ID, so the driver can't differentiate between.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/jiffies.h>
#include <linux/platform_device.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/err.h>
#include <linux/mutex.h>
#include <linux/sysfs.h>
#include <linux/ioport.h>
#include <linux/acpi.h>
#include <linux/io.h>

static unsigned short force_id;
module_param(force_id, ushort, 0);
MODULE_PARM_DESC(force_id, "Override the detected device ID");

static struct platform_device *pdev;

#define DRVNAME "f71805f"
enum kinds { f71805f, f71872f };

/*
 * Super-I/O constants and functions
 */

#define F71805F_LD_HWM		0x04

#define SIO_REG_LDSEL		0x07	/* Logical device select */
#define SIO_REG_DEVID		0x20	/* Device ID (2 bytes) */
#define SIO_REG_DEVREV		0x22	/* Device revision */
#define SIO_REG_MANID		0x23	/* Fintek ID (2 bytes) */
#define SIO_REG_FNSEL1		0x29	/* Multi Function Select 1 (F71872F) */
#define SIO_REG_ENABLE		0x30	/* Logical device enable */
#define SIO_REG_ADDR		0x60	/* Logical device address (2 bytes) */

#define SIO_FINTEK_ID		0x1934
#define SIO_F71805F_ID		0x0406
#define SIO_F71872F_ID		0x0341

static inline int
superio_inb(int base, int reg)
{
	outb(reg, base);
	return inb(base + 1);
}

static int
superio_inw(int base, int reg)
{
	int val;
	outb(reg++, base);
	val = inb(base + 1) << 8;
	outb(reg, base);
	val |= inb(base + 1);
	return val;
}

static inline void
superio_select(int base, int ld)
{
	outb(SIO_REG_LDSEL, base);
	outb(ld, base + 1);
}

static inline int
superio_enter(int base)
{
	if (!request_muxed_region(base, 2, DRVNAME))
		return -EBUSY;

	outb(0x87, base);
	outb(0x87, base);

	return 0;
}

static inline void
superio_exit(int base)
{
	outb(0xaa, base);
	release_region(base, 2);
}

/*
 * ISA constants
 */

#define REGION_LENGTH		8
#define ADDR_REG_OFFSET		5
#define DATA_REG_OFFSET		6

/*
 * Registers
 */

/* in nr from 0 to 10 (8-bit values) */
#define F71805F_REG_IN(nr)		(0x10 + (nr))
#define F71805F_REG_IN_HIGH(nr)		((nr) < 10 ? 0x40 + 2 * (nr) : 0x2E)
#define F71805F_REG_IN_LOW(nr)		((nr) < 10 ? 0x41 + 2 * (nr) : 0x2F)
/* fan nr from 0 to 2 (12-bit values, two registers) */
#define F71805F_REG_FAN(nr)		(0x20 + 2 * (nr))
#define F71805F_REG_FAN_LOW(nr)		(0x28 + 2 * (nr))
#define F71805F_REG_FAN_TARGET(nr)	(0x69 + 16 * (nr))
#define F71805F_REG_FAN_CTRL(nr)	(0x60 + 16 * (nr))
#define F71805F_REG_PWM_FREQ(nr)	(0x63 + 16 * (nr))
#define F71805F_REG_PWM_DUTY(nr)	(0x6B + 16 * (nr))
/* temp nr from 0 to 2 (8-bit values) */
#define F71805F_REG_TEMP(nr)		(0x1B + (nr))
#define F71805F_REG_TEMP_HIGH(nr)	(0x54 + 2 * (nr))
#define F71805F_REG_TEMP_HYST(nr)	(0x55 + 2 * (nr))
#define F71805F_REG_TEMP_MODE		0x01
/* pwm/fan pwmnr from 0 to 2, auto point apnr from 0 to 2 */
/* map Fintek numbers to our numbers as follows: 9->0, 5->1, 1->2 */
#define F71805F_REG_PWM_AUTO_POINT_TEMP(pwmnr, apnr) \
					(0xA0 + 0x10 * (pwmnr) + (2 - (apnr)))
#define F71805F_REG_PWM_AUTO_POINT_FAN(pwmnr, apnr) \
					(0xA4 + 0x10 * (pwmnr) + \
						2 * (2 - (apnr)))

#define F71805F_REG_START		0x00
/* status nr from 0 to 2 */
#define F71805F_REG_STATUS(nr)		(0x36 + (nr))

/* individual register bits */
#define FAN_CTRL_DC_MODE		0x10
#define FAN_CTRL_LATCH_FULL		0x08
#define FAN_CTRL_MODE_MASK		0x03
#define FAN_CTRL_MODE_SPEED		0x00
#define FAN_CTRL_MODE_TEMPERATURE	0x01
#define FAN_CTRL_MODE_MANUAL		0x02

/*
 * Data structures and manipulation thereof
 */

struct f71805f_auto_point {
	u8 temp[3];
	u16 fan[3];
};

struct f71805f_data {
	unsigned short addr;
	const char *name;
	struct device *hwmon_dev;

	struct mutex update_lock;
	bool valid;		/* true if following fields are valid */
	unsigned long last_updated;	/* In jiffies */
	unsigned long last_limits;	/* In jiffies */

	/* Register values */
	u8 in[11];
	u8 in_high[11];
	u8 in_low[11];
	u16 has_in;
	u16 fan[3];
	u16 fan_low[3];
	u16 fan_target[3];
	u8 fan_ctrl[3];
	u8 pwm[3];
	u8 pwm_freq[3];
	u8 temp[3];
	u8 temp_high[3];
	u8 temp_hyst[3];
	u8 temp_mode;
	unsigned long alarms;
	struct f71805f_auto_point auto_points[3];
};

struct f71805f_sio_data {
	enum kinds kind;
	u8 fnsel1;
};

static inline long in_from_reg(u8 reg)
{
	return reg * 8;
}

/* The 2 least significant bits are not used */
static inline u8 in_to_reg(long val)
{
	if (val <= 0)
		return 0;
	if (val >= 2016)
		return 0xfc;
	return ((val + 16) / 32) << 2;
}

/* in0 is downscaled by a factor 2 internally */
static inline long in0_from_reg(u8 reg)
{
	return reg * 16;
}

static inline u8 in0_to_reg(long val)
{
	if (val <= 0)
		return 0;
	if (val >= 4032)
		return 0xfc;
	return ((val + 32) / 64) << 2;
}

/* The 4 most significant bits are not used */
static inline long fan_from_reg(u16 reg)
{
	reg &= 0xfff;
	if (!reg || reg == 0xfff)
		return 0;
	return 1500000 / reg;
}

static inline u16 fan_to_reg(long rpm)
{
	/*
	 * If the low limit is set below what the chip can measure,
	 * store the largest possible 12-bit value in the registers,
	 * so that no alarm will ever trigger.
	 */
	if (rpm < 367)
		return 0xfff;
	return 1500000 / rpm;
}

static inline unsigned long pwm_freq_from_reg(u8 reg)
{
	unsigned long clock = (reg & 0x80) ? 48000000UL : 1000000UL;

	reg &= 0x7f;
	if (reg == 0)
		reg++;
	return clock / (reg << 8);
}

static inline u8 pwm_freq_to_reg(unsigned long val)
{
	if (val >= 187500)	/* The highest we can do */
		return 0x80;
	if (val >= 1475)	/* Use 48 MHz clock */
		return 0x80 | (48000000UL / (val << 8));
	if (val < 31)		/* The lowest we can do */
		return 0x7f;
	else			/* Use 1 MHz clock */
		return 1000000UL / (val << 8);
}

static inline int pwm_mode_from_reg(u8 reg)
{
	return !(reg & FAN_CTRL_DC_MODE);
}

static inline long temp_from_reg(u8 reg)
{
	return reg * 1000;
}

static inline u8 temp_to_reg(long val)
{
	if (val <= 0)
		return 0;
	if (val >= 1000 * 0xff)
		return 0xff;
	return (val + 500) / 1000;
}

/*
 * Device I/O access
 */

/* Must be called with data->update_lock held, except during initialization */
static u8 f71805f_read8(struct f71805f_data *data, u8 reg)
{
	outb(reg, data->addr + ADDR_REG_OFFSET);
	return inb(data->addr + DATA_REG_OFFSET);
}

/* Must be called with data->update_lock held, except during initialization */
static void f71805f_write8(struct f71805f_data *data, u8 reg, u8 val)
{
	outb(reg, data->addr + ADDR_REG_OFFSET);
	outb(val, data->addr + DATA_REG_OFFSET);
}

/*
 * It is important to read the MSB first, because doing so latches the
 * value of the LSB, so we are sure both bytes belong to the same value.
 * Must be called with data->update_lock held, except during initialization
 */
static u16 f71805f_read16(struct f71805f_data *data, u8 reg)
{
	u16 val;

	outb(reg, data->addr + ADDR_REG_OFFSET);
	val = inb(data->addr + DATA_REG_OFFSET) << 8;
	outb(++reg, data->addr + ADDR_REG_OFFSET);
	val |= inb(data->addr + DATA_REG_OFFSET);

	return val;
}

/* Must be called with data->update_lock held, except during initialization */
static void f71805f_write16(struct f71805f_data *data, u8 reg, u16 val)
{
	outb(reg, data->addr + ADDR_REG_OFFSET);
	outb(val >> 8, data->addr + DATA_REG_OFFSET);
	outb(++reg, data->addr + ADDR_REG_OFFSET);
	outb(val & 0xff, data->addr + DATA_REG_OFFSET);
}

static struct f71805f_data *f71805f_update_device(struct device *dev)
{
	struct f71805f_data *data = dev_get_drvdata(dev);
	int nr, apnr;

	mutex_lock(&data->update_lock);

	/* Limit registers cache is refreshed after 60 seconds */
	if (time_after(jiffies, data->last_updated + 60 * HZ)
	 || !data->valid) {
		for (nr = 0; nr < 11; nr++) {
			if (!(data->has_in & (1 << nr)))
				continue;
			data->in_high[nr] = f71805f_read8(data,
					    F71805F_REG_IN_HIGH(nr));
			data->in_low[nr] = f71805f_read8(data,
					   F71805F_REG_IN_LOW(nr));
		}
		for (nr = 0; nr < 3; nr++) {
			data->fan_low[nr] = f71805f_read16(data,
					    F71805F_REG_FAN_LOW(nr));
			data->fan_target[nr] = f71805f_read16(data,
					       F71805F_REG_FAN_TARGET(nr));
			data->pwm_freq[nr] = f71805f_read8(data,
					     F71805F_REG_PWM_FREQ(nr));
		}
		for (nr = 0; nr < 3; nr++) {
			data->temp_high[nr] = f71805f_read8(data,
					      F71805F_REG_TEMP_HIGH(nr));
			data->temp_hyst[nr] = f71805f_read8(data,
					      F71805F_REG_TEMP_HYST(nr));
		}
		data->temp_mode = f71805f_read8(data, F71805F_REG_TEMP_MODE);
		for (nr = 0; nr < 3; nr++) {
			for (apnr = 0; apnr < 3; apnr++) {
				data->auto_points[nr].temp[apnr] =
					f71805f_read8(data,
					F71805F_REG_PWM_AUTO_POINT_TEMP(nr,
									apnr));
				data->auto_points[nr].fan[apnr] =
					f71805f_read16(data,
					F71805F_REG_PWM_AUTO_POINT_FAN(nr,
								       apnr));
			}
		}

		data->last_limits = jiffies;
	}

	/* Measurement registers cache is refreshed after 1 second */
	if (time_after(jiffies, data->last_updated + HZ)
	 || !data->valid) {
		for (nr = 0; nr < 11; nr++) {
			if (!(data->has_in & (1 << nr)))
				continue;
			data->in[nr] = f71805f_read8(data,
				       F71805F_REG_IN(nr));
		}
		for (nr = 0; nr < 3; nr++) {
			data->fan[nr] = f71805f_read16(data,
					F71805F_REG_FAN(nr));
			data->fan_ctrl[nr] = f71805f_read8(data,
					     F71805F_REG_FAN_CTRL(nr));
			data->pwm[nr] = f71805f_read8(data,
					F71805F_REG_PWM_DUTY(nr));
		}
		for (nr = 0; nr < 3; nr++) {
			data->temp[nr] = f71805f_read8(data,
					 F71805F_REG_TEMP(nr));
		}
		data->alarms = f71805f_read8(data, F71805F_REG_STATUS(0))
			+ (f71805f_read8(data, F71805F_REG_STATUS(1)) << 8)
			+ (f71805f_read8(data, F71805F_REG_STATUS(2)) << 16);

		data->last_updated = jiffies;
		data->valid = true;
	}

	mutex_unlock(&data->update_lock);

	return data;
}

/*
 * Sysfs interface
 */

static ssize_t show_in0(struct device *dev, struct device_attribute *devattr,
			char *buf)
{
	struct f71805f_data *data = f71805f_update_device(dev);
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	int nr = attr->index;

	return sprintf(buf, "%ld\n", in0_from_reg(data->in[nr]));
}

static ssize_t show_in0_max(struct device *dev, struct device_attribute
			    *devattr, char *buf)
{
	struct f71805f_data *data = f71805f_update_device(dev);
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	int nr = attr->index;

	return sprintf(buf, "%ld\n", in0_from_reg(data->in_high[nr]));
}

static ssize_t show_in0_min(struct device *dev, struct device_attribute
			    *devattr, char *buf)
{
	struct f71805f_data *data = f71805f_update_device(dev);
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	int nr = attr->index;

	return sprintf(buf, "%ld\n", in0_from_reg(data->in_low[nr]));
}

static ssize_t set_in0_max(struct device *dev, struct device_attribute
			   *devattr, const char *buf, size_t count)
{
	struct f71805f_data *data = dev_get_drvdata(dev);
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	int nr = attr->index;
	long val;
	int err;

	err = kstrtol(buf, 10, &val);
	if (err)
		return err;

	mutex_lock(&data->update_lock);
	data->in_high[nr] = in0_to_reg(val);
	f71805f_write8(data, F71805F_REG_IN_HIGH(nr), data->in_high[nr]);
	mutex_unlock(&data->update_lock);

	return count;
}

static ssize_t set_in0_min(struct device *dev, struct device_attribute
			   *devattr, const char *buf, size_t count)
{
	struct f71805f_data *data = dev_get_drvdata(dev);
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	int nr = attr->index;
	long val;
	int err;

	err = kstrtol(buf, 10, &val);
	if (err)
		return err;

	mutex_lock(&data->update_lock);
	data->in_low[nr] = in0_to_reg(val);
	f71805f_write8(data, F71805F_REG_IN_LOW(nr), data->in_low[nr]);
	mutex_unlock(&data->update_lock);

	return count;
}

static ssize_t show_in(struct device *dev, struct device_attribute *devattr,
		       char *buf)
{
	struct f71805f_data *data = f71805f_update_device(dev);
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	int nr = attr->index;

	return sprintf(buf, "%ld\n", in_from_reg(data->in[nr]));
}

static ssize_t show_in_max(struct device *dev, struct device_attribute
			   *devattr, char *buf)
{
	struct f71805f_data *data = f71805f_update_device(dev);
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	int nr = attr->index;

	return sprintf(buf, "%ld\n", in_from_reg(data->in_high[nr]));
}

static ssize_t show_in_min(struct device *dev, struct device_attribute
			   *devattr, char *buf)
{
	struct f71805f_data *data = f71805f_update_device(dev);
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	int nr = attr->index;

	return sprintf(buf, "%ld\n", in_from_reg(data->in_low[nr]));
}

static ssize_t set_in_max(struct device *dev, struct device_attribute
			  *devattr, const char *buf, size_t count)
{
	struct f71805f_data *data = dev_get_drvdata(dev);
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	int nr = attr->index;
	long val;
	int err;

	err = kstrtol(buf, 10, &val);
	if (err)
		return err;

	mutex_lock(&data->update_lock);
	data->in_high[nr] = in_to_reg(val);
	f71805f_write8(data, F71805F_REG_IN_HIGH(nr), data->in_high[nr]);
	mutex_unlock(&data->update_lock);

	return count;
}

static ssize_t set_in_min(struct device *dev, struct device_attribute
			  *devattr, const char *buf, size_t count)
{
	struct f71805f_data *data = dev_get_drvdata(dev);
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	int nr = attr->index;
	long val;
	int err;

	err = kstrtol(buf, 10, &val);
	if (err)
		return err;

	mutex_lock(&data->update_lock);
	data->in_low[nr] = in_to_reg(val);
	f71805f_write8(data, F71805F_REG_IN_LOW(nr), data->in_low[nr]);
	mutex_unlock(&data->update_lock);

	return count;
}

static ssize_t show_fan(struct device *dev, struct device_attribute *devattr,
			char *buf)
{
	struct f71805f_data *data = f71805f_update_device(dev);
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	int nr = attr->index;

	return sprintf(buf, "%ld\n", fan_from_reg(data->fan[nr]));
}

static ssize_t show_fan_min(struct device *dev, struct device_attribute
			    *devattr, char *buf)
{
	struct f71805f_data *data = f71805f_update_device(dev);
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	int nr = attr->index;

	return sprintf(buf, "%ld\n", fan_from_reg(data->fan_low[nr]));
}

static ssize_t show_fan_target(struct device *dev, struct device_attribute
			       *devattr, char *buf)
{
	struct f71805f_data *data = f71805f_update_device(dev);
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	int nr = attr->index;

	return sprintf(buf, "%ld\n", fan_from_reg(data->fan_target[nr]));
}

static ssize_t set_fan_min(struct device *dev, struct device_attribute
			   *devattr, const char *buf, size_t count)
{
	struct f71805f_data *data = dev_get_drvdata(dev);
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	int nr = attr->index;
	long val;
	int err;

	err = kstrtol(buf, 10, &val);
	if (err)
		return err;

	mutex_lock(&data->update_lock);
	data->fan_low[nr] = fan_to_reg(val);
	f71805f_write16(data, F71805F_REG_FAN_LOW(nr), data->fan_low[nr]);
	mutex_unlock(&data->update_lock);

	return count;
}

static ssize_t set_fan_target(struct device *dev, struct device_attribute
			      *devattr, const char *buf, size_t count)
{
	struct f71805f_data *data = dev_get_drvdata(dev);
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	int nr = attr->index;
	long val;
	int err;

	err = kstrtol(buf, 10, &val);
	if (err)
		return err;

	mutex_lock(&data->update_lock);
	data->fan_target[nr] = fan_to_reg(val);
	f71805f_write16(data, F71805F_REG_FAN_TARGET(nr),
			data->fan_target[nr]);
	mutex_unlock(&data->update_lock);

	return count;
}

static ssize_t show_pwm(struct device *dev, struct device_attribute *devattr,
			char *buf)
{
	struct f71805f_data *data = f71805f_update_device(dev);
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	int nr = attr->index;

	return sprintf(buf, "%d\n", (int)data->pwm[nr]);
}

static ssize_t show_pwm_enable(struct device *dev, struct device_attribute
			       *devattr, char *buf)
{
	struct f71805f_data *data = f71805f_update_device(dev);
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	int nr = attr->index;
	int mode;

	switch (data->fan_ctrl[nr] & FAN_CTRL_MODE_MASK) {
	case FAN_CTRL_MODE_SPEED:
		mode = 3;
		break;
	case FAN_CTRL_MODE_TEMPERATURE:
		mode = 2;
		break;
	default: /* MANUAL */
		mode = 1;
	}

	return sprintf(buf, "%d\n", mode);
}

static ssize_t show_pwm_freq(struct device *dev, struct device_attribute
			     *devattr, char *buf)
{
	struct f71805f_data *data = f71805f_update_device(dev);
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	int nr = attr->index;

	return sprintf(buf, "%lu\n", pwm_freq_from_reg(data->pwm_freq[nr]));
}

static ssize_t show_pwm_mode(struct device *dev, struct device_attribute
			     *devattr, char *buf)
{
	struct f71805f_data *data = f71805f_update_device(dev);
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	int nr = attr->index;

	return sprintf(buf, "%d\n", pwm_mode_from_reg(data->fan_ctrl[nr]));
}

static ssize_t set_pwm(struct device *dev, struct device_attribute *devattr,
		       const char *buf, size_t count)
{
	struct f71805f_data *data = dev_get_drvdata(dev);
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	int nr = attr->index;
	unsigned long val;
	int err;

	err = kstrtoul(buf, 10, &val);
	if (err)
		return err;

	if (val > 255)
		return -EINVAL;

	mutex_lock(&data->update_lock);
	data->pwm[nr] = val;
	f71805f_write8(data, F71805F_REG_PWM_DUTY(nr), data->pwm[nr]);
	mutex_unlock(&data->update_lock);

	return count;
}

static struct attribute *f71805f_attr_pwm[];

static ssize_t set_pwm_enable(struct device *dev, struct device_attribute
			      *devattr, const char *buf, size_t count)
{
	struct f71805f_data *data = dev_get_drvdata(dev);
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	int nr = attr->index;
	u8 reg;
	unsigned long val;
	int err;

	err = kstrtoul(buf, 10, &val);
	if (err)
		return err;

	if (val < 1 || val > 3)
		return -EINVAL;

	if (val > 1) { /* Automatic mode, user can't set PWM value */
		if (sysfs_chmod_file(&dev->kobj, f71805f_attr_pwm[nr],
				     S_IRUGO))
			dev_dbg(dev, "chmod -w pwm%d failed\n", nr + 1);
	}

	mutex_lock(&data->update_lock);
	reg = f71805f_read8(data, F71805F_REG_FAN_CTRL(nr))
	    & ~FAN_CTRL_MODE_MASK;
	switch (val) {
	case 1:
		reg |= FAN_CTRL_MODE_MANUAL;
		break;
	case 2:
		reg |= FAN_CTRL_MODE_TEMPERATURE;
		break;
	case 3:
		reg |= FAN_CTRL_MODE_SPEED;
		break;
	}
	data->fan_ctrl[nr] = reg;
	f71805f_write8(data, F71805F_REG_FAN_CTRL(nr), reg);
	mutex_unlock(&data->update_lock);

	if (val == 1) { /* Manual mode, user can set PWM value */
		if (sysfs_chmod_file(&dev->kobj, f71805f_attr_pwm[nr],
				     S_IRUGO | S_IWUSR))
			dev_dbg(dev, "chmod +w pwm%d failed\n", nr + 1);
	}

	return count;
}

static ssize_t set_pwm_freq(struct device *dev, struct device_attribute
			    *devattr, const char *buf, size_t count)
{
	struct f71805f_data *data = dev_get_drvdata(dev);
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	int nr = attr->index;
	unsigned long val;
	int err;

	err = kstrtoul(buf, 10, &val);
	if (err)
		return err;

	mutex_lock(&data->update_lock);
	data->pwm_freq[nr] = pwm_freq_to_reg(val);
	f71805f_write8(data, F71805F_REG_PWM_FREQ(nr), data->pwm_freq[nr]);
	mutex_unlock(&data->update_lock);

	return count;
}

static ssize_t show_pwm_auto_point_temp(struct device *dev,
					struct device_attribute *devattr,
					char *buf)
{
	struct f71805f_data *data = dev_get_drvdata(dev);
	struct sensor_device_attribute_2 *attr = to_sensor_dev_attr_2(devattr);
	int pwmnr = attr->nr;
	int apnr = attr->index;

	return sprintf(buf, "%ld\n",
		       temp_from_reg(data->auto_points[pwmnr].temp[apnr]));
}

static ssize_t set_pwm_auto_point_temp(struct device *dev,
				       struct device_attribute *devattr,
				       const char *buf, size_t count)
{
	struct f71805f_data *data = dev_get_drvdata(dev);
	struct sensor_device_attribute_2 *attr = to_sensor_dev_attr_2(devattr);
	int pwmnr = attr->nr;
	int apnr = attr->index;
	unsigned long val;
	int err;

	err = kstrtoul(buf, 10, &val);
	if (err)
		return err;

	mutex_lock(&data->update_lock);
	data->auto_points[pwmnr].temp[apnr] = temp_to_reg(val);
	f71805f_write8(data, F71805F_REG_PWM_AUTO_POINT_TEMP(pwmnr, apnr),
		       data->auto_points[pwmnr].temp[apnr]);
	mutex_unlock(&data->update_lock);

	return count;
}

static ssize_t show_pwm_auto_point_fan(struct device *dev,
				       struct device_attribute *devattr,
				       char *buf)
{
	struct f71805f_data *data = dev_get_drvdata(dev);
	struct sensor_device_attribute_2 *attr = to_sensor_dev_attr_2(devattr);
	int pwmnr = attr->nr;
	int apnr = attr->index;

	return sprintf(buf, "%ld\n",
		       fan_from_reg(data->auto_points[pwmnr].fan[apnr]));
}

static ssize_t set_pwm_auto_point_fan(struct device *dev,
				      struct device_attribute *devattr,
				      const char *buf, size_t count)
{
	struct f71805f_data *data = dev_get_drvdata(dev);
	struct sensor_device_attribute_2 *attr = to_sensor_dev_attr_2(devattr);
	int pwmnr = attr->nr;
	int apnr = attr->index;
	unsigned long val;
	int err;

	err = kstrtoul(buf, 10, &val);
	if (err)
		return err;

	mutex_lock(&data->update_lock);
	data->auto_points[pwmnr].fan[apnr] = fan_to_reg(val);
	f71805f_write16(data, F71805F_REG_PWM_AUTO_POINT_FAN(pwmnr, apnr),
			data->auto_points[pwmnr].fan[apnr]);
	mutex_unlock(&data->update_lock);

	return count;
}

static ssize_t show_temp(struct device *dev, struct device_attribute *devattr,
			 char *buf)
{
	struct f71805f_data *data = f71805f_update_device(dev);
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	int nr = attr->index;

	return sprintf(buf, "%ld\n", temp_from_reg(data->temp[nr]));
}

static ssize_t show_temp_max(struct device *dev, struct device_attribute
			     *devattr, char *buf)
{
	struct f71805f_data *data = f71805f_update_device(dev);
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	int nr = attr->index;

	return sprintf(buf, "%ld\n", temp_from_reg(data->temp_high[nr]));
}

static ssize_t show_temp_hyst(struct device *dev, struct device_attribute
			      *devattr, char *buf)
{
	struct f71805f_data *data = f71805f_update_device(dev);
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	int nr = attr->index;

	return sprintf(buf, "%ld\n", temp_from_reg(data->temp_hyst[nr]));
}

static ssize_t show_temp_type(struct device *dev, struct device_attribute
			      *devattr, char *buf)
{
	struct f71805f_data *data = f71805f_update_device(dev);
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	int nr = attr->index;

	/* 3 is diode, 4 is thermistor */
	return sprintf(buf, "%u\n", (data->temp_mode & (1 << nr)) ? 3 : 4);
}

static ssize_t set_temp_max(struct device *dev, struct device_attribute
			    *devattr, const char *buf, size_t count)
{
	struct f71805f_data *data = dev_get_drvdata(dev);
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	int nr = attr->index;
	long val;
	int err;

	err = kstrtol(buf, 10, &val);
	if (err)
		return err;

	mutex_lock(&data->update_lock);
	data->temp_high[nr] = temp_to_reg(val);
	f71805f_write8(data, F71805F_REG_TEMP_HIGH(nr), data->temp_high[nr]);
	mutex_unlock(&data->update_lock);

	return count;
}

static ssize_t set_temp_hyst(struct device *dev, struct device_attribute
			     *devattr, const char *buf, size_t count)
{
	struct f71805f_data *data = dev_get_drvdata(dev);
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	int nr = attr->index;
	long val;
	int err;

	err = kstrtol(buf, 10, &val);
	if (err)
		return err;

	mutex_lock(&data->update_lock);
	data->temp_hyst[nr] = temp_to_reg(val);
	f71805f_write8(data, F71805F_REG_TEMP_HYST(nr), data->temp_hyst[nr]);
	mutex_unlock(&data->update_lock);

	return count;
}

static ssize_t alarms_in_show(struct device *dev, struct device_attribute
			      *devattr, char *buf)
{
	struct f71805f_data *data = f71805f_update_device(dev);

	return sprintf(buf, "%lu\n", data->alarms & 0x7ff);
}

static ssize_t alarms_fan_show(struct device *dev, struct device_attribute
			       *devattr, char *buf)
{
	struct f71805f_data *data = f71805f_update_device(dev);

	return sprintf(buf, "%lu\n", (data->alarms >> 16) & 0x07);
}

static ssize_t alarms_temp_show(struct device *dev, struct device_attribute
				*devattr, char *buf)
{
	struct f71805f_data *data = f71805f_update_device(dev);

	return sprintf(buf, "%lu\n", (data->alarms >> 11) & 0x07);
}

static ssize_t show_alarm(struct device *dev, struct device_attribute
			  *devattr, char *buf)
{
	struct f71805f_data *data = f71805f_update_device(dev);
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	int bitnr = attr->index;

	return sprintf(buf, "%lu\n", (data->alarms >> bitnr) & 1);
}

static ssize_t name_show(struct device *dev, struct device_attribute
			 *devattr, char *buf)
{
	struct f71805f_data *data = dev_get_drvdata(dev);

	return sprintf(buf, "%s\n", data->name);
}

static SENSOR_DEVICE_ATTR(in0_input, S_IRUGO, show_in0, NULL, 0);
static SENSOR_DEVICE_ATTR(in0_max, S_IRUGO | S_IWUSR,
			  show_in0_max, set_in0_max, 0);
static SENSOR_DEVICE_ATTR(in0_min, S_IRUGO | S_IWUSR,
			  show_in0_min, set_in0_min, 0);
static SENSOR_DEVICE_ATTR(in1_input, S_IRUGO, show_in, NULL, 1);
static SENSOR_DEVICE_ATTR(in1_max, S_IRUGO | S_IWUSR,
			  show_in_max, set_in_max, 1);
static SENSOR_DEVICE_ATTR(in1_min, S_IRUGO | S_IWUSR,
			  show_in_min, set_in_min, 1);
static SENSOR_DEVICE_ATTR(in2_input, S_IRUGO, show_in, NULL, 2);
static SENSOR_DEVICE_ATTR(in2_max, S_IRUGO | S_IWUSR,
			  show_in_max, set_in_max, 2);
static SENSOR_DEVICE_ATTR(in2_min, S_IRUGO | S_IWUSR,
			  show_in_min, set_in_min, 2);
static SENSOR_DEVICE_ATTR(in3_input, S_IRUGO, show_in, NULL, 3);
static SENSOR_DEVICE_ATTR(in3_max, S_IRUGO | S_IWUSR,
			  show_in_max, set_in_max, 3);
static SENSOR_DEVICE_ATTR(in3_min, S_IRUGO | S_IWUSR,
			  show_in_min, set_in_min, 3);
static SENSOR_DEVICE_ATTR(in4_input, S_IRUGO, show_in, NULL, 4);
static SENSOR_DEVICE_ATTR(in4_max, S_IRUGO | S_IWUSR,
			  show_in_max, set_in_max, 4);
static SENSOR_DEVICE_ATTR(in4_min, S_IRUGO | S_IWUSR,
			  show_in_min, set_in_min, 4);
static SENSOR_DEVICE_ATTR(in5_input, S_IRUGO, show_in, NULL, 5);
static SENSOR_DEVICE_ATTR(in5_max, S_IRUGO | S_IWUSR,
			  show_in_max, set_in_max, 5);
static SENSOR_DEVICE_ATTR(in5_min, S_IRUGO | S_IWUSR,
			  show_in_min, set_in_min, 5);
static SENSOR_DEVICE_ATTR(in6_input, S_IRUGO, show_in, NULL, 6);
static SENSOR_DEVICE_ATTR(in6_max, S_IRUGO | S_IWUSR,
			  show_in_max, set_in_max, 6);
static SENSOR_DEVICE_ATTR(in6_min, S_IRUGO | S_IWUSR,
			  show_in_min, set_in_min, 6);
static SENSOR_DEVICE_ATTR(in7_input, S_IRUGO, show_in, NULL, 7);
static SENSOR_DEVICE_ATTR(in7_max, S_IRUGO | S_IWUSR,
			  show_in_max, set_in_max, 7);
static SENSOR_DEVICE_ATTR(in7_min, S_IRUGO | S_IWUSR,
			  show_in_min, set_in_min, 7);
static SENSOR_DEVICE_ATTR(in8_input, S_IRUGO, show_in, NULL, 8);
static SENSOR_DEVICE_ATTR(in8_max, S_IRUGO | S_IWUSR,
			  show_in_max, set_in_max, 8);
static SENSOR_DEVICE_ATTR(in8_min, S_IRUGO | S_IWUSR,
			  show_in_min, set_in_min, 8);
static SENSOR_DEVICE_ATTR(in9_input, S_IRUGO, show_in0, NULL, 9);
static SENSOR_DEVICE_ATTR(in9_max, S_IRUGO | S_IWUSR,
			  show_in0_max, set_in0_max, 9);
static SENSOR_DEVICE_ATTR(in9_min, S_IRUGO | S_IWUSR,
			  show_in0_min, set_in0_min, 9);
static SENSOR_DEVICE_ATTR(in10_input, S_IRUGO, show_in0, NULL, 10);
static SENSOR_DEVICE_ATTR(in10_max, S_IRUGO | S_IWUSR,
			  show_in0_max, set_in0_max, 10);
static SENSOR_DEVICE_ATTR(in10_min, S_IRUGO | S_IWUSR,
			  show_in0_min, set_in0_min, 10);

static SENSOR_DEVICE_ATTR(fan1_input, S_IRUGO, show_fan, NULL, 0);
static SENSOR_DEVICE_ATTR(fan1_min, S_IRUGO | S_IWUSR,
			  show_fan_min, set_fan_min, 0);
static SENSOR_DEVICE_ATTR(fan1_target, S_IRUGO | S_IWUSR,
			  show_fan_target, set_fan_target, 0);
static SENSOR_DEVICE_ATTR(fan2_input, S_IRUGO, show_fan, NULL, 1);
static SENSOR_DEVICE_ATTR(fan2_min, S_IRUGO | S_IWUSR,
			  show_fan_min, set_fan_min, 1);
static SENSOR_DEVICE_ATTR(fan2_target, S_IRUGO | S_IWUSR,
			  show_fan_target, set_fan_target, 1);
static SENSOR_DEVICE_ATTR(fan3_input, S_IRUGO, show_fan, NULL, 2);
static SENSOR_DEVICE_ATTR(fan3_min, S_IRUGO | S_IWUSR,
			  show_fan_min, set_fan_min, 2);
static SENSOR_DEVICE_ATTR(fan3_target, S_IRUGO | S_IWUSR,
			  show_fan_target, set_fan_target, 2);

static SENSOR_DEVICE_ATTR(temp1_input, S_IRUGO, show_temp, NULL, 0);
static SENSOR_DEVICE_ATTR(temp1_max, S_IRUGO | S_IWUSR,
		    show_temp_max, set_temp_max, 0);
static SENSOR_DEVICE_ATTR(temp1_max_hyst, S_IRUGO | S_IWUSR,
		    show_temp_hyst, set_temp_hyst, 0);
static SENSOR_DEVICE_ATTR(temp1_type, S_IRUGO, show_temp_type, NULL, 0);
static SENSOR_DEVICE_ATTR(temp2_input, S_IRUGO, show_temp, NULL, 1);
static SENSOR_DEVICE_ATTR(temp2_max, S_IRUGO | S_IWUSR,
		    show_temp_max, set_temp_max, 1);
static SENSOR_DEVICE_ATTR(temp2_max_hyst, S_IRUGO | S_IWUSR,
		    show_temp_hyst, set_temp_hyst, 1);
static SENSOR_DEVICE_ATTR(temp2_type, S_IRUGO, show_temp_type, NULL, 1);
static SENSOR_DEVICE_ATTR(temp3_input, S_IRUGO, show_temp, NULL, 2);
static SENSOR_DEVICE_ATTR(temp3_max, S_IRUGO | S_IWUSR,
		    show_temp_max, set_temp_max, 2);
static SENSOR_DEVICE_ATTR(temp3_max_hyst, S_IRUGO | S_IWUSR,
		    show_temp_hyst, set_temp_hyst, 2);
static SENSOR_DEVICE_ATTR(temp3_type, S_IRUGO, show_temp_type, NULL, 2);

/*
 * pwm (value) files are created read-only, write permission is
 * then added or removed dynamically as needed
 */
static SENSOR_DEVICE_ATTR(pwm1, S_IRUGO, show_pwm, set_pwm, 0);
static SENSOR_DEVICE_ATTR(pwm1_enable, S_IRUGO | S_IWUSR,
			  show_pwm_enable, set_pwm_enable, 0);
static SENSOR_DEVICE_ATTR(pwm1_freq, S_IRUGO | S_IWUSR,
			  show_pwm_freq, set_pwm_freq, 0);
static SENSOR_DEVICE_ATTR(pwm1_mode, S_IRUGO, show_pwm_mode, NULL, 0);
static SENSOR_DEVICE_ATTR(pwm2, S_IRUGO, show_pwm, set_pwm, 1);
static SENSOR_DEVICE_ATTR(pwm2_enable, S_IRUGO | S_IWUSR,
			  show_pwm_enable, set_pwm_enable, 1);
static SENSOR_DEVICE_ATTR(pwm2_freq, S_IRUGO | S_IWUSR,
			  show_pwm_freq, set_pwm_freq, 1);
static SENSOR_DEVICE_ATTR(pwm2_mode, S_IRUGO, show_pwm_mode, NULL, 1);
static SENSOR_DEVICE_ATTR(pwm3, S_IRUGO, show_pwm, set_pwm, 2);
static SENSOR_DEVICE_ATTR(pwm3_enable, S_IRUGO | S_IWUSR,
			  show_pwm_enable, set_pwm_enable, 2);
static SENSOR_DEVICE_ATTR(pwm3_freq, S_IRUGO | S_IWUSR,
			  show_pwm_freq, set_pwm_freq, 2);
static SENSOR_DEVICE_ATTR(pwm3_mode, S_IRUGO, show_pwm_mode, NULL, 2);

static SENSOR_DEVICE_ATTR_2(pwm1_auto_point1_temp, S_IRUGO | S_IWUSR,
			    show_pwm_auto_point_temp, set_pwm_auto_point_temp,
			    0, 0);
static SENSOR_DEVICE_ATTR_2(pwm1_auto_point1_fan, S_IRUGO | S_IWUSR,
			    show_pwm_auto_point_fan, set_pwm_auto_point_fan,
			    0, 0);
static SENSOR_DEVICE_ATTR_2(pwm1_auto_point2_temp, S_IRUGO | S_IWUSR,
			    show_pwm_auto_point_temp, set_pwm_auto_point_temp,
			    0, 1);
static SENSOR_DEVICE_ATTR_2(pwm1_auto_point2_fan, S_IRUGO | S_IWUSR,
			    show_pwm_auto_point_fan, set_pwm_auto_point_fan,
			    0, 1);
static SENSOR_DEVICE_ATTR_2(pwm1_auto_point3_temp, S_IRUGO | S_IWUSR,
			    show_pwm_auto_point_temp, set_pwm_auto_point_temp,
			    0, 2);
static SENSOR_DEVICE_ATTR_2(pwm1_auto_point3_fan, S_IRUGO | S_IWUSR,
			    show_pwm_auto_point_fan, set_pwm_auto_point_fan,
			    0, 2);

static SENSOR_DEVICE_ATTR_2(pwm2_auto_point1_temp, S_IRUGO | S_IWUSR,
			    show_pwm_auto_point_temp, set_pwm_auto_point_temp,
			    1, 0);
static SENSOR_DEVICE_ATTR_2(pwm2_auto_point1_fan, S_IRUGO | S_IWUSR,
			    show_pwm_auto_point_fan, set_pwm_auto_point_fan,
			    1, 0);
static SENSOR_DEVICE_ATTR_2(pwm2_auto_point2_temp, S_IRUGO | S_IWUSR,
			    show_pwm_auto_point_temp, set_pwm_auto_point_temp,
			    1, 1);
static SENSOR_DEVICE_ATTR_2(pwm2_auto_point2_fan, S_IRUGO | S_IWUSR,
			    show_pwm_auto_point_fan, set_pwm_auto_point_fan,
			    1, 1);
static SENSOR_DEVICE_ATTR_2(pwm2_auto_point3_temp, S_IRUGO | S_IWUSR,
			    show_pwm_auto_point_temp, set_pwm_auto_point_temp,
			    1, 2);
static SENSOR_DEVICE_ATTR_2(pwm2_auto_point3_fan, S_IRUGO | S_IWUSR,
			    show_pwm_auto_point_fan, set_pwm_auto_point_fan,
			    1, 2);

static SENSOR_DEVICE_ATTR_2(pwm3_auto_point1_temp, S_IRUGO | S_IWUSR,
			    show_pwm_auto_point_temp, set_pwm_auto_point_temp,
			    2, 0);
static SENSOR_DEVICE_ATTR_2(pwm3_auto_point1_fan, S_IRUGO | S_IWUSR,
			    show_pwm_auto_point_fan, set_pwm_auto_point_fan,
			    2, 0);
static SENSOR_DEVICE_ATTR_2(pwm3_auto_point2_temp, S_IRUGO | S_IWUSR,
			    show_pwm_auto_point_temp, set_pwm_auto_point_temp,
			    2, 1);
static SENSOR_DEVICE_ATTR_2(pwm3_auto_point2_fan, S_IRUGO | S_IWUSR,
			    show_pwm_auto_point_fan, set_pwm_auto_point_fan,
			    2, 1);
static SENSOR_DEVICE_ATTR_2(pwm3_auto_point3_temp, S_IRUGO | S_IWUSR,
			    show_pwm_auto_point_temp, set_pwm_auto_point_temp,
			    2, 2);
static SENSOR_DEVICE_ATTR_2(pwm3_auto_point3_fan, S_IRUGO | S_IWUSR,
			    show_pwm_auto_point_fan, set_pwm_auto_point_fan,
			    2, 2);

static SENSOR_DEVICE_ATTR(in0_alarm, S_IRUGO, show_alarm, NULL, 0);
static SENSOR_DEVICE_ATTR(in1_alarm, S_IRUGO, show_alarm, NULL, 1);
static SENSOR_DEVICE_ATTR(in2_alarm, S_IRUGO, show_alarm, NULL, 2);
static SENSOR_DEVICE_ATTR(in3_alarm, S_IRUGO, show_alarm, NULL, 3);
static SENSOR_DEVICE_ATTR(in4_alarm, S_IRUGO, show_alarm, NULL, 4);
static SENSOR_DEVICE_ATTR(in5_alarm, S_IRUGO, show_alarm, NULL, 5);
static SENSOR_DEVICE_ATTR(in6_alarm, S_IRUGO, show_alarm, NULL, 6);
static SENSOR_DEVICE_ATTR(in7_alarm, S_IRUGO, show_alarm, NULL, 7);
static SENSOR_DEVICE_ATTR(in8_alarm, S_IRUGO, show_alarm, NULL, 8);
static SENSOR_DEVICE_ATTR(in9_alarm, S_IRUGO, show_alarm, NULL, 9);
static SENSOR_DEVICE_ATTR(in10_alarm, S_IRUGO, show_alarm, NULL, 10);
static SENSOR_DEVICE_ATTR(temp1_alarm, S_IRUGO, show_alarm, NULL, 11);
static SENSOR_DEVICE_ATTR(temp2_alarm, S_IRUGO, show_alarm, NULL, 12);
static SENSOR_DEVICE_ATTR(temp3_alarm, S_IRUGO, show_alarm, NULL, 13);
static SENSOR_DEVICE_ATTR(fan1_alarm, S_IRUGO, show_alarm, NULL, 16);
static SENSOR_DEVICE_ATTR(fan2_alarm, S_IRUGO, show_alarm, NULL, 17);
static SENSOR_DEVICE_ATTR(fan3_alarm, S_IRUGO, show_alarm, NULL, 18);
static DEVICE_ATTR_RO(alarms_in);
static DEVICE_ATTR_RO(alarms_fan);
static DEVICE_ATTR_RO(alarms_temp);

static DEVICE_ATTR_RO(name);

static struct attribute *f71805f_attributes[] = {
	&sensor_dev_attr_in0_input.dev_attr.attr,
	&sensor_dev_attr_in0_max.dev_attr.attr,
	&sensor_dev_attr_in0_min.dev_attr.attr,
	&sensor_dev_attr_in1_input.dev_attr.attr,
	&sensor_dev_attr_in1_max.dev_attr.attr,
	&sensor_dev_attr_in1_min.dev_attr.attr,
	&sensor_dev_attr_in2_input.dev_attr.attr,
	&sensor_dev_attr_in2_max.dev_attr.attr,
	&sensor_dev_attr_in2_min.dev_attr.attr,
	&sensor_dev_attr_in3_input.dev_attr.attr,
	&sensor_dev_attr_in3_max.dev_attr.attr,
	&sensor_dev_attr_in3_min.dev_attr.attr,
	&sensor_dev_attr_in5_input.dev_attr.attr,
	&sensor_dev_attr_in5_max.dev_attr.attr,
	&sensor_dev_attr_in5_min.dev_attr.attr,
	&sensor_dev_attr_in6_input.dev_attr.attr,
	&sensor_dev_attr_in6_max.dev_attr.attr,
	&sensor_dev_attr_in6_min.dev_attr.attr,
	&sensor_dev_attr_in7_input.dev_attr.attr,
	&sensor_dev_attr_in7_max.dev_attr.attr,
	&sensor_dev_attr_in7_min.dev_attr.attr,

	&sensor_dev_attr_fan1_input.dev_attr.attr,
	&sensor_dev_attr_fan1_min.dev_attr.attr,
	&sensor_dev_attr_fan1_alarm.dev_attr.attr,
	&sensor_dev_attr_fan1_target.dev_attr.attr,
	&sensor_dev_attr_fan2_input.dev_attr.attr,
	&sensor_dev_attr_fan2_min.dev_attr.attr,
	&sensor_dev_attr_fan2_alarm.dev_attr.attr,
	&sensor_dev_attr_fan2_target.dev_attr.attr,
	&sensor_dev_attr_fan3_input.dev_attr.attr,
	&sensor_dev_attr_fan3_min.dev_attr.attr,
	&sensor_dev_attr_fan3_alarm.dev_attr.attr,
	&sensor_dev_attr_fan3_target.dev_attr.attr,

	&sensor_dev_attr_pwm1.dev_attr.attr,
	&sensor_dev_attr_pwm1_enable.dev_attr.attr,
	&sensor_dev_attr_pwm1_mode.dev_attr.attr,
	&sensor_dev_attr_pwm2.dev_attr.attr,
	&sensor_dev_attr_pwm2_enable.dev_attr.attr,
	&sensor_dev_attr_pwm2_mode.dev_attr.attr,
	&sensor_dev_attr_pwm3.dev_attr.attr,
	&sensor_dev_attr_pwm3_enable.dev_attr.attr,
	&sensor_dev_attr_pwm3_mode.dev_attr.attr,

	&sensor_dev_attr_temp1_input.dev_attr.attr,
	&sensor_dev_attr_temp1_max.dev_attr.attr,
	&sensor_dev_attr_temp1_max_hyst.dev_attr.attr,
	&sensor_dev_attr_temp1_type.dev_attr.attr,
	&sensor_dev_attr_temp2_input.dev_attr.attr,
	&sensor_dev_attr_temp2_max.dev_attr.attr,
	&sensor_dev_attr_temp2_max_hyst.dev_attr.attr,
	&sensor_dev_attr_temp2_type.dev_attr.attr,
	&sensor_dev_attr_temp3_input.dev_attr.attr,
	&sensor_dev_attr_temp3_max.dev_attr.attr,
	&sensor_dev_attr_temp3_max_hyst.dev_attr.attr,
	&sensor_dev_attr_temp3_type.dev_attr.attr,

	&sensor_dev_attr_pwm1_auto_point1_temp.dev_attr.attr,
	&sensor_dev_attr_pwm1_auto_point1_fan.dev_attr.attr,
	&sensor_dev_attr_pwm1_auto_point2_temp.dev_attr.attr,
	&sensor_dev_attr_pwm1_auto_point2_fan.dev_attr.attr,
	&sensor_dev_attr_pwm1_auto_point3_temp.dev_attr.attr,
	&sensor_dev_attr_pwm1_auto_point3_fan.dev_attr.attr,
	&sensor_dev_attr_pwm2_auto_point1_temp.dev_attr.attr,
	&sensor_dev_attr_pwm2_auto_point1_fan.dev_attr.attr,
	&sensor_dev_attr_pwm2_auto_point2_temp.dev_attr.attr,
	&sensor_dev_attr_pwm2_auto_point2_fan.dev_attr.attr,
	&sensor_dev_attr_pwm2_auto_point3_temp.dev_attr.attr,
	&sensor_dev_attr_pwm2_auto_point3_fan.dev_attr.attr,
	&sensor_dev_attr_pwm3_auto_point1_temp.dev_attr.attr,
	&sensor_dev_attr_pwm3_auto_point1_fan.dev_attr.attr,
	&sensor_dev_attr_pwm3_auto_point2_temp.dev_attr.attr,
	&sensor_dev_attr_pwm3_auto_point2_fan.dev_attr.attr,
	&sensor_dev_attr_pwm3_auto_point3_temp.dev_attr.attr,
	&sensor_dev_attr_pwm3_auto_point3_fan.dev_attr.attr,

	&sensor_dev_attr_in0_alarm.dev_attr.attr,
	&sensor_dev_attr_in1_alarm.dev_attr.attr,
	&sensor_dev_attr_in2_alarm.dev_attr.attr,
	&sensor_dev_attr_in3_alarm.dev_attr.attr,
	&sensor_dev_attr_in5_alarm.dev_attr.attr,
	&sensor_dev_attr_in6_alarm.dev_attr.attr,
	&sensor_dev_attr_in7_alarm.dev_attr.attr,
	&dev_attr_alarms_in.attr,
	&sensor_dev_attr_temp1_alarm.dev_attr.attr,
	&sensor_dev_attr_temp2_alarm.dev_attr.attr,
	&sensor_dev_attr_temp3_alarm.dev_attr.attr,
	&dev_attr_alarms_temp.attr,
	&dev_attr_alarms_fan.attr,

	&dev_attr_name.attr,
	NULL
};

static const struct attribute_group f71805f_group = {
	.attrs = f71805f_attributes,
};

static struct attribute *f71805f_attributes_optin[4][5] = {
	{
		&sensor_dev_attr_in4_input.dev_attr.attr,
		&sensor_dev_attr_in4_max.dev_attr.attr,
		&sensor_dev_attr_in4_min.dev_attr.attr,
		&sensor_dev_attr_in4_alarm.dev_attr.attr,
		NULL
	}, {
		&sensor_dev_attr_in8_input.dev_attr.attr,
		&sensor_dev_attr_in8_max.dev_attr.attr,
		&sensor_dev_attr_in8_min.dev_attr.attr,
		&sensor_dev_attr_in8_alarm.dev_attr.attr,
		NULL
	}, {
		&sensor_dev_attr_in9_input.dev_attr.attr,
		&sensor_dev_attr_in9_max.dev_attr.attr,
		&sensor_dev_attr_in9_min.dev_attr.attr,
		&sensor_dev_attr_in9_alarm.dev_attr.attr,
		NULL
	}, {
		&sensor_dev_attr_in10_input.dev_attr.attr,
		&sensor_dev_attr_in10_max.dev_attr.attr,
		&sensor_dev_attr_in10_min.dev_attr.attr,
		&sensor_dev_attr_in10_alarm.dev_attr.attr,
		NULL
	}
};

static const struct attribute_group f71805f_group_optin[4] = {
	{ .attrs = f71805f_attributes_optin[0] },
	{ .attrs = f71805f_attributes_optin[1] },
	{ .attrs = f71805f_attributes_optin[2] },
	{ .attrs = f71805f_attributes_optin[3] },
};

/*
 * We don't include pwm_freq files in the arrays above, because they must be
 * created conditionally (only if pwm_mode is 1 == PWM)
 */
static struct attribute *f71805f_attributes_pwm_freq[] = {
	&sensor_dev_attr_pwm1_freq.dev_attr.attr,
	&sensor_dev_attr_pwm2_freq.dev_attr.attr,
	&sensor_dev_attr_pwm3_freq.dev_attr.attr,
	NULL
};

static const struct attribute_group f71805f_group_pwm_freq = {
	.attrs = f71805f_attributes_pwm_freq,
};

/* We also need an indexed access to pwmN files to toggle writability */
static struct attribute *f71805f_attr_pwm[] = {
	&sensor_dev_attr_pwm1.dev_attr.attr,
	&sensor_dev_attr_pwm2.dev_attr.attr,
	&sensor_dev_attr_pwm3.dev_attr.attr,
};

/*
 * Device registration and initialization
 */

static void f71805f_init_device(struct f71805f_data *data)
{
	u8 reg;
	int i;

	reg = f71805f_read8(data, F71805F_REG_START);
	if ((reg & 0x41) != 0x01) {
		pr_debug("Starting monitoring operations\n");
		f71805f_write8(data, F71805F_REG_START, (reg | 0x01) & ~0x40);
	}

	/*
	 * Fan monitoring can be disabled. If it is, we won't be polling
	 * the register values, and won't create the related sysfs files.
	 */
	for (i = 0; i < 3; i++) {
		data->fan_ctrl[i] = f71805f_read8(data,
						  F71805F_REG_FAN_CTRL(i));
		/*
		 * Clear latch full bit, else "speed mode" fan speed control
		 * doesn't work
		 */
		if (data->fan_ctrl[i] & FAN_CTRL_LATCH_FULL) {
			data->fan_ctrl[i] &= ~FAN_CTRL_LATCH_FULL;
			f71805f_write8(data, F71805F_REG_FAN_CTRL(i),
				       data->fan_ctrl[i]);
		}
	}
}

static int f71805f_probe(struct platform_device *pdev)
{
	struct f71805f_sio_data *sio_data = dev_get_platdata(&pdev->dev);
	struct f71805f_data *data;
	struct resource *res;
	int i, err;

	static const char * const names[] = {
		"f71805f",
		"f71872f",
	};

	data = devm_kzalloc(&pdev->dev, sizeof(struct f71805f_data),
			    GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_IO, 0);
	if (!devm_request_region(&pdev->dev, res->start + ADDR_REG_OFFSET, 2,
				 DRVNAME)) {
		dev_err(&pdev->dev, "Failed to request region 0x%lx-0x%lx\n",
			(unsigned long)(res->start + ADDR_REG_OFFSET),
			(unsigned long)(res->start + ADDR_REG_OFFSET + 1));
		return -EBUSY;
	}
	data->addr = res->start;
	data->name = names[sio_data->kind];
	mutex_init(&data->update_lock);

	platform_set_drvdata(pdev, data);

	/* Some voltage inputs depend on chip model and configuration */
	switch (sio_data->kind) {
	case f71805f:
		data->has_in = 0x1ff;
		break;
	case f71872f:
		data->has_in = 0x6ef;
		if (sio_data->fnsel1 & 0x01)
			data->has_in |= (1 << 4); /* in4 */
		if (sio_data->fnsel1 & 0x02)
			data->has_in |= (1 << 8); /* in8 */
		break;
	}

	/* Initialize the F71805F chip */
	f71805f_init_device(data);

	/* Register sysfs interface files */
	err = sysfs_create_group(&pdev->dev.kobj, &f71805f_group);
	if (err)
		return err;
	if (data->has_in & (1 << 4)) { /* in4 */
		err = sysfs_create_group(&pdev->dev.kobj,
					 &f71805f_group_optin[0]);
		if (err)
			goto exit_remove_files;
	}
	if (data->has_in & (1 << 8)) { /* in8 */
		err = sysfs_create_group(&pdev->dev.kobj,
					 &f71805f_group_optin[1]);
		if (err)
			goto exit_remove_files;
	}
	if (data->has_in & (1 << 9)) { /* in9 (F71872F/FG only) */
		err = sysfs_create_group(&pdev->dev.kobj,
					 &f71805f_group_optin[2]);
		if (err)
			goto exit_remove_files;
	}
	if (data->has_in & (1 << 10)) { /* in9 (F71872F/FG only) */
		err = sysfs_create_group(&pdev->dev.kobj,
					 &f71805f_group_optin[3]);
		if (err)
			goto exit_remove_files;
	}
	for (i = 0; i < 3; i++) {
		/* If control mode is PWM, create pwm_freq file */
		if (!(data->fan_ctrl[i] & FAN_CTRL_DC_MODE)) {
			err = sysfs_create_file(&pdev->dev.kobj,
						f71805f_attributes_pwm_freq[i]);
			if (err)
				goto exit_remove_files;
		}
		/* If PWM is in manual mode, add write permission */
		if (data->fan_ctrl[i] & FAN_CTRL_MODE_MANUAL) {
			err = sysfs_chmod_file(&pdev->dev.kobj,
					       f71805f_attr_pwm[i],
					       S_IRUGO | S_IWUSR);
			if (err) {
				dev_err(&pdev->dev, "chmod +w pwm%d failed\n",
					i + 1);
				goto exit_remove_files;
			}
		}
	}

	data->hwmon_dev = hwmon_device_register(&pdev->dev);
	if (IS_ERR(data->hwmon_dev)) {
		err = PTR_ERR(data->hwmon_dev);
		dev_err(&pdev->dev, "Class registration failed (%d)\n", err);
		goto exit_remove_files;
	}

	return 0;

exit_remove_files:
	sysfs_remove_group(&pdev->dev.kobj, &f71805f_group);
	for (i = 0; i < 4; i++)
		sysfs_remove_group(&pdev->dev.kobj, &f71805f_group_optin[i]);
	sysfs_remove_group(&pdev->dev.kobj, &f71805f_group_pwm_freq);
	return err;
}

static void f71805f_remove(struct platform_device *pdev)
{
	struct f71805f_data *data = platform_get_drvdata(pdev);
	int i;

	hwmon_device_unregister(data->hwmon_dev);
	sysfs_remove_group(&pdev->dev.kobj, &f71805f_group);
	for (i = 0; i < 4; i++)
		sysfs_remove_group(&pdev->dev.kobj, &f71805f_group_optin[i]);
	sysfs_remove_group(&pdev->dev.kobj, &f71805f_group_pwm_freq);
}

static struct platform_driver f71805f_driver = {
	.driver = {
		.name	= DRVNAME,
	},
	.probe		= f71805f_probe,
	.remove_new	= f71805f_remove,
};

static int __init f71805f_device_add(unsigned short address,
				     const struct f71805f_sio_data *sio_data)
{
	struct resource res = {
		.start	= address,
		.end	= address + REGION_LENGTH - 1,
		.flags	= IORESOURCE_IO,
	};
	int err;

	pdev = platform_device_alloc(DRVNAME, address);
	if (!pdev) {
		err = -ENOMEM;
		pr_err("Device allocation failed\n");
		goto exit;
	}

	res.name = pdev->name;
	err = acpi_check_resource_conflict(&res);
	if (err)
		goto exit_device_put;

	err = platform_device_add_resources(pdev, &res, 1);
	if (err) {
		pr_err("Device resource addition failed (%d)\n", err);
		goto exit_device_put;
	}

	err = platform_device_add_data(pdev, sio_data,
				       sizeof(struct f71805f_sio_data));
	if (err) {
		pr_err("Platform data allocation failed\n");
		goto exit_device_put;
	}

	err = platform_device_add(pdev);
	if (err) {
		pr_err("Device addition failed (%d)\n", err);
		goto exit_device_put;
	}

	return 0;

exit_device_put:
	platform_device_put(pdev);
exit:
	return err;
}

static int __init f71805f_find(int sioaddr, unsigned short *address,
			       struct f71805f_sio_data *sio_data)
{
	int err;
	u16 devid;

	static const char * const names[] = {
		"F71805F/FG",
		"F71872F/FG or F71806F/FG",
	};

	err = superio_enter(sioaddr);
	if (err)
		return err;

	err = -ENODEV;
	devid = superio_inw(sioaddr, SIO_REG_MANID);
	if (devid != SIO_FINTEK_ID)
		goto exit;

	devid = force_id ? force_id : superio_inw(sioaddr, SIO_REG_DEVID);
	switch (devid) {
	case SIO_F71805F_ID:
		sio_data->kind = f71805f;
		break;
	case SIO_F71872F_ID:
		sio_data->kind = f71872f;
		sio_data->fnsel1 = superio_inb(sioaddr, SIO_REG_FNSEL1);
		break;
	default:
		pr_info("Unsupported Fintek device, skipping\n");
		goto exit;
	}

	superio_select(sioaddr, F71805F_LD_HWM);
	if (!(superio_inb(sioaddr, SIO_REG_ENABLE) & 0x01)) {
		pr_warn("Device not activated, skipping\n");
		goto exit;
	}

	*address = superio_inw(sioaddr, SIO_REG_ADDR);
	if (*address == 0) {
		pr_warn("Base address not set, skipping\n");
		goto exit;
	}
	*address &= ~(REGION_LENGTH - 1);	/* Ignore 3 LSB */

	err = 0;
	pr_info("Found %s chip at %#x, revision %u\n",
		names[sio_data->kind], *address,
		superio_inb(sioaddr, SIO_REG_DEVREV));

exit:
	superio_exit(sioaddr);
	return err;
}

static int __init f71805f_init(void)
{
	int err;
	unsigned short address;
	struct f71805f_sio_data sio_data;

	if (f71805f_find(0x2e, &address, &sio_data)
	 && f71805f_find(0x4e, &address, &sio_data))
		return -ENODEV;

	err = platform_driver_register(&f71805f_driver);
	if (err)
		goto exit;

	/* Sets global pdev as a side effect */
	err = f71805f_device_add(address, &sio_data);
	if (err)
		goto exit_driver;

	return 0;

exit_driver:
	platform_driver_unregister(&f71805f_driver);
exit:
	return err;
}

static void __exit f71805f_exit(void)
{
	platform_device_unregister(pdev);
	platform_driver_unregister(&f71805f_driver);
}

MODULE_AUTHOR("Jean Delvare <jdelvare@suse.de>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("F71805F/F71872F hardware monitoring driver");

module_init(f71805f_init);
module_exit(f71805f_exit);
