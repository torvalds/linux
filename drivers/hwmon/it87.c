/*
 *  it87.c - Part of lm_sensors, Linux kernel modules for hardware
 *           monitoring.
 *
 *  The IT8705F is an LPC-based Super I/O part that contains UARTs, a
 *  parallel port, an IR port, a MIDI port, a floppy controller, etc., in
 *  addition to an Environment Controller (Enhanced Hardware Monitor and
 *  Fan Controller)
 *
 *  This driver supports only the Environment Controller in the IT8705F and
 *  similar parts.  The other devices are supported by different drivers.
 *
 *  Supports: IT8705F  Super I/O chip w/LPC interface
 *            IT8712F  Super I/O chip w/LPC interface
 *            IT8716F  Super I/O chip w/LPC interface
 *            IT8718F  Super I/O chip w/LPC interface
 *            IT8720F  Super I/O chip w/LPC interface
 *            IT8721F  Super I/O chip w/LPC interface
 *            IT8726F  Super I/O chip w/LPC interface
 *            IT8758E  Super I/O chip w/LPC interface
 *            Sis950   A clone of the IT8705F
 *
 *  Copyright (C) 2001 Chris Gauthron
 *  Copyright (C) 2005-2010 Jean Delvare <khali@linux-fr.org>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/jiffies.h>
#include <linux/platform_device.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/hwmon-vid.h>
#include <linux/err.h>
#include <linux/mutex.h>
#include <linux/sysfs.h>
#include <linux/string.h>
#include <linux/dmi.h>
#include <linux/acpi.h>
#include <linux/io.h>

#define DRVNAME "it87"

enum chips { it87, it8712, it8716, it8718, it8720, it8721 };

static unsigned short force_id;
module_param(force_id, ushort, 0);
MODULE_PARM_DESC(force_id, "Override the detected device ID");

static struct platform_device *pdev;

#define	REG	0x2e	/* The register to read/write */
#define	DEV	0x07	/* Register: Logical device select */
#define	VAL	0x2f	/* The value to read/write */
#define PME	0x04	/* The device with the fan registers in it */

/* The device with the IT8718F/IT8720F VID value in it */
#define GPIO	0x07

#define	DEVID	0x20	/* Register: Device ID */
#define	DEVREV	0x22	/* Register: Device Revision */

static inline int superio_inb(int reg)
{
	outb(reg, REG);
	return inb(VAL);
}

static inline void superio_outb(int reg, int val)
{
	outb(reg, REG);
	outb(val, VAL);
}

static int superio_inw(int reg)
{
	int val;
	outb(reg++, REG);
	val = inb(VAL) << 8;
	outb(reg, REG);
	val |= inb(VAL);
	return val;
}

static inline void superio_select(int ldn)
{
	outb(DEV, REG);
	outb(ldn, VAL);
}

static inline int superio_enter(void)
{
	/*
	 * Try to reserve REG and REG + 1 for exclusive access.
	 */
	if (!request_muxed_region(REG, 2, DRVNAME))
		return -EBUSY;

	outb(0x87, REG);
	outb(0x01, REG);
	outb(0x55, REG);
	outb(0x55, REG);
	return 0;
}

static inline void superio_exit(void)
{
	outb(0x02, REG);
	outb(0x02, VAL);
	release_region(REG, 2);
}

/* Logical device 4 registers */
#define IT8712F_DEVID 0x8712
#define IT8705F_DEVID 0x8705
#define IT8716F_DEVID 0x8716
#define IT8718F_DEVID 0x8718
#define IT8720F_DEVID 0x8720
#define IT8721F_DEVID 0x8721
#define IT8726F_DEVID 0x8726
#define IT87_ACT_REG  0x30
#define IT87_BASE_REG 0x60

/* Logical device 7 registers (IT8712F and later) */
#define IT87_SIO_GPIO3_REG	0x27
#define IT87_SIO_GPIO5_REG	0x29
#define IT87_SIO_PINX2_REG	0x2c	/* Pin selection */
#define IT87_SIO_VID_REG	0xfc	/* VID value */
#define IT87_SIO_BEEP_PIN_REG	0xf6	/* Beep pin mapping */

/* Update battery voltage after every reading if true */
static int update_vbat;

/* Not all BIOSes properly configure the PWM registers */
static int fix_pwm_polarity;

/* Many IT87 constants specified below */

/* Length of ISA address segment */
#define IT87_EXTENT 8

/* Length of ISA address segment for Environmental Controller */
#define IT87_EC_EXTENT 2

/* Offset of EC registers from ISA base address */
#define IT87_EC_OFFSET 5

/* Where are the ISA address/data registers relative to the EC base address */
#define IT87_ADDR_REG_OFFSET 0
#define IT87_DATA_REG_OFFSET 1

/*----- The IT87 registers -----*/

#define IT87_REG_CONFIG        0x00

#define IT87_REG_ALARM1        0x01
#define IT87_REG_ALARM2        0x02
#define IT87_REG_ALARM3        0x03

/* The IT8718F and IT8720F have the VID value in a different register, in
   Super-I/O configuration space. */
#define IT87_REG_VID           0x0a
/* The IT8705F and IT8712F earlier than revision 0x08 use register 0x0b
   for fan divisors. Later IT8712F revisions must use 16-bit tachometer
   mode. */
#define IT87_REG_FAN_DIV       0x0b
#define IT87_REG_FAN_16BIT     0x0c

/* Monitors: 9 voltage (0 to 7, battery), 3 temp (1 to 3), 3 fan (1 to 3) */

static const u8 IT87_REG_FAN[]		= { 0x0d, 0x0e, 0x0f, 0x80, 0x82 };
static const u8 IT87_REG_FAN_MIN[]	= { 0x10, 0x11, 0x12, 0x84, 0x86 };
static const u8 IT87_REG_FANX[]		= { 0x18, 0x19, 0x1a, 0x81, 0x83 };
static const u8 IT87_REG_FANX_MIN[]	= { 0x1b, 0x1c, 0x1d, 0x85, 0x87 };
#define IT87_REG_FAN_MAIN_CTRL 0x13
#define IT87_REG_FAN_CTL       0x14
#define IT87_REG_PWM(nr)       (0x15 + (nr))
#define IT87_REG_PWM_DUTY(nr)  (0x63 + (nr) * 8)

#define IT87_REG_VIN(nr)       (0x20 + (nr))
#define IT87_REG_TEMP(nr)      (0x29 + (nr))

#define IT87_REG_VIN_MAX(nr)   (0x30 + (nr) * 2)
#define IT87_REG_VIN_MIN(nr)   (0x31 + (nr) * 2)
#define IT87_REG_TEMP_HIGH(nr) (0x40 + (nr) * 2)
#define IT87_REG_TEMP_LOW(nr)  (0x41 + (nr) * 2)

#define IT87_REG_VIN_ENABLE    0x50
#define IT87_REG_TEMP_ENABLE   0x51
#define IT87_REG_BEEP_ENABLE   0x5c

#define IT87_REG_CHIPID        0x58

#define IT87_REG_AUTO_TEMP(nr, i) (0x60 + (nr) * 8 + (i))
#define IT87_REG_AUTO_PWM(nr, i)  (0x65 + (nr) * 8 + (i))


struct it87_sio_data {
	enum chips type;
	/* Values read from Super-I/O config space */
	u8 revision;
	u8 vid_value;
	u8 beep_pin;
	u8 internal;	/* Internal sensors can be labeled */
	/* Features skipped based on config or DMI */
	u8 skip_vid;
	u8 skip_fan;
	u8 skip_pwm;
};

/* For each registered chip, we need to keep some data in memory.
   The structure is dynamically allocated. */
struct it87_data {
	struct device *hwmon_dev;
	enum chips type;
	u8 revision;

	unsigned short addr;
	const char *name;
	struct mutex update_lock;
	char valid;		/* !=0 if following fields are valid */
	unsigned long last_updated;	/* In jiffies */

	u16 in_scaled;		/* Internal voltage sensors are scaled */
	u8 in[9];		/* Register value */
	u8 in_max[8];		/* Register value */
	u8 in_min[8];		/* Register value */
	u8 has_fan;		/* Bitfield, fans enabled */
	u16 fan[5];		/* Register values, possibly combined */
	u16 fan_min[5];		/* Register values, possibly combined */
	s8 temp[3];		/* Register value */
	s8 temp_high[3];	/* Register value */
	s8 temp_low[3];		/* Register value */
	u8 sensor;		/* Register value */
	u8 fan_div[3];		/* Register encoding, shifted right */
	u8 vid;			/* Register encoding, combined */
	u8 vrm;
	u32 alarms;		/* Register encoding, combined */
	u8 beeps;		/* Register encoding */
	u8 fan_main_ctrl;	/* Register value */
	u8 fan_ctl;		/* Register value */

	/* The following 3 arrays correspond to the same registers up to
	 * the IT8720F. The meaning of bits 6-0 depends on the value of bit
	 * 7, and we want to preserve settings on mode changes, so we have
	 * to track all values separately.
	 * Starting with the IT8721F, the manual PWM duty cycles are stored
	 * in separate registers (8-bit values), so the separate tracking
	 * is no longer needed, but it is still done to keep the driver
	 * simple. */
	u8 pwm_ctrl[3];		/* Register value */
	u8 pwm_duty[3];		/* Manual PWM value set by user */
	u8 pwm_temp_map[3];	/* PWM to temp. chan. mapping (bits 1-0) */

	/* Automatic fan speed control registers */
	u8 auto_pwm[3][4];	/* [nr][3] is hard-coded */
	s8 auto_temp[3][5];	/* [nr][0] is point1_temp_hyst */
};

static u8 in_to_reg(const struct it87_data *data, int nr, long val)
{
	long lsb;

	if (data->type == it8721) {
		if (data->in_scaled & (1 << nr))
			lsb = 24;
		else
			lsb = 12;
	} else
		lsb = 16;

	val = DIV_ROUND_CLOSEST(val, lsb);
	return SENSORS_LIMIT(val, 0, 255);
}

static int in_from_reg(const struct it87_data *data, int nr, int val)
{
	if (data->type == it8721) {
		if (data->in_scaled & (1 << nr))
			return val * 24;
		else
			return val * 12;
	} else
		return val * 16;
}

static inline u8 FAN_TO_REG(long rpm, int div)
{
	if (rpm == 0)
		return 255;
	rpm = SENSORS_LIMIT(rpm, 1, 1000000);
	return SENSORS_LIMIT((1350000 + rpm * div / 2) / (rpm * div), 1,
			     254);
}

static inline u16 FAN16_TO_REG(long rpm)
{
	if (rpm == 0)
		return 0xffff;
	return SENSORS_LIMIT((1350000 + rpm) / (rpm * 2), 1, 0xfffe);
}

#define FAN_FROM_REG(val, div) ((val) == 0 ? -1 : (val) == 255 ? 0 : \
				1350000 / ((val) * (div)))
/* The divider is fixed to 2 in 16-bit mode */
#define FAN16_FROM_REG(val) ((val) == 0 ? -1 : (val) == 0xffff ? 0 : \
			     1350000 / ((val) * 2))

#define TEMP_TO_REG(val) (SENSORS_LIMIT(((val) < 0 ? (((val) - 500) / 1000) : \
					((val) + 500) / 1000), -128, 127))
#define TEMP_FROM_REG(val) ((val) * 1000)

static u8 pwm_to_reg(const struct it87_data *data, long val)
{
	if (data->type == it8721)
		return val;
	else
		return val >> 1;
}

static int pwm_from_reg(const struct it87_data *data, u8 reg)
{
	if (data->type == it8721)
		return reg;
	else
		return (reg & 0x7f) << 1;
}


static int DIV_TO_REG(int val)
{
	int answer = 0;
	while (answer < 7 && (val >>= 1))
		answer++;
	return answer;
}
#define DIV_FROM_REG(val) (1 << (val))

static const unsigned int pwm_freq[8] = {
	48000000 / 128,
	24000000 / 128,
	12000000 / 128,
	8000000 / 128,
	6000000 / 128,
	3000000 / 128,
	1500000 / 128,
	750000 / 128,
};

static inline int has_16bit_fans(const struct it87_data *data)
{
	/* IT8705F Datasheet 0.4.1, 3h == Version G.
	   IT8712F Datasheet 0.9.1, section 8.3.5 indicates 8h == Version J.
	   These are the first revisions with 16bit tachometer support. */
	return (data->type == it87 && data->revision >= 0x03)
	    || (data->type == it8712 && data->revision >= 0x08)
	    || data->type == it8716
	    || data->type == it8718
	    || data->type == it8720
	    || data->type == it8721;
}

static inline int has_old_autopwm(const struct it87_data *data)
{
	/* The old automatic fan speed control interface is implemented
	   by IT8705F chips up to revision F and IT8712F chips up to
	   revision G. */
	return (data->type == it87 && data->revision < 0x03)
	    || (data->type == it8712 && data->revision < 0x08);
}

static int it87_probe(struct platform_device *pdev);
static int __devexit it87_remove(struct platform_device *pdev);

static int it87_read_value(struct it87_data *data, u8 reg);
static void it87_write_value(struct it87_data *data, u8 reg, u8 value);
static struct it87_data *it87_update_device(struct device *dev);
static int it87_check_pwm(struct device *dev);
static void it87_init_device(struct platform_device *pdev);


static struct platform_driver it87_driver = {
	.driver = {
		.owner	= THIS_MODULE,
		.name	= DRVNAME,
	},
	.probe	= it87_probe,
	.remove	= __devexit_p(it87_remove),
};

static ssize_t show_in(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr);
	int nr = sensor_attr->index;

	struct it87_data *data = it87_update_device(dev);
	return sprintf(buf, "%d\n", in_from_reg(data, nr, data->in[nr]));
}

static ssize_t show_in_min(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr);
	int nr = sensor_attr->index;

	struct it87_data *data = it87_update_device(dev);
	return sprintf(buf, "%d\n", in_from_reg(data, nr, data->in_min[nr]));
}

static ssize_t show_in_max(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr);
	int nr = sensor_attr->index;

	struct it87_data *data = it87_update_device(dev);
	return sprintf(buf, "%d\n", in_from_reg(data, nr, data->in_max[nr]));
}

static ssize_t set_in_min(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr);
	int nr = sensor_attr->index;

	struct it87_data *data = dev_get_drvdata(dev);
	unsigned long val;

	if (strict_strtoul(buf, 10, &val) < 0)
		return -EINVAL;

	mutex_lock(&data->update_lock);
	data->in_min[nr] = in_to_reg(data, nr, val);
	it87_write_value(data, IT87_REG_VIN_MIN(nr),
			data->in_min[nr]);
	mutex_unlock(&data->update_lock);
	return count;
}
static ssize_t set_in_max(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr);
	int nr = sensor_attr->index;

	struct it87_data *data = dev_get_drvdata(dev);
	unsigned long val;

	if (strict_strtoul(buf, 10, &val) < 0)
		return -EINVAL;

	mutex_lock(&data->update_lock);
	data->in_max[nr] = in_to_reg(data, nr, val);
	it87_write_value(data, IT87_REG_VIN_MAX(nr),
			data->in_max[nr]);
	mutex_unlock(&data->update_lock);
	return count;
}

#define show_in_offset(offset)					\
static SENSOR_DEVICE_ATTR(in##offset##_input, S_IRUGO,		\
		show_in, NULL, offset);

#define limit_in_offset(offset)					\
static SENSOR_DEVICE_ATTR(in##offset##_min, S_IRUGO | S_IWUSR,	\
		show_in_min, set_in_min, offset);		\
static SENSOR_DEVICE_ATTR(in##offset##_max, S_IRUGO | S_IWUSR,	\
		show_in_max, set_in_max, offset);

show_in_offset(0);
limit_in_offset(0);
show_in_offset(1);
limit_in_offset(1);
show_in_offset(2);
limit_in_offset(2);
show_in_offset(3);
limit_in_offset(3);
show_in_offset(4);
limit_in_offset(4);
show_in_offset(5);
limit_in_offset(5);
show_in_offset(6);
limit_in_offset(6);
show_in_offset(7);
limit_in_offset(7);
show_in_offset(8);

/* 3 temperatures */
static ssize_t show_temp(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr);
	int nr = sensor_attr->index;

	struct it87_data *data = it87_update_device(dev);
	return sprintf(buf, "%d\n", TEMP_FROM_REG(data->temp[nr]));
}
static ssize_t show_temp_max(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr);
	int nr = sensor_attr->index;

	struct it87_data *data = it87_update_device(dev);
	return sprintf(buf, "%d\n", TEMP_FROM_REG(data->temp_high[nr]));
}
static ssize_t show_temp_min(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr);
	int nr = sensor_attr->index;

	struct it87_data *data = it87_update_device(dev);
	return sprintf(buf, "%d\n", TEMP_FROM_REG(data->temp_low[nr]));
}
static ssize_t set_temp_max(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr);
	int nr = sensor_attr->index;

	struct it87_data *data = dev_get_drvdata(dev);
	long val;

	if (strict_strtol(buf, 10, &val) < 0)
		return -EINVAL;

	mutex_lock(&data->update_lock);
	data->temp_high[nr] = TEMP_TO_REG(val);
	it87_write_value(data, IT87_REG_TEMP_HIGH(nr), data->temp_high[nr]);
	mutex_unlock(&data->update_lock);
	return count;
}
static ssize_t set_temp_min(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr);
	int nr = sensor_attr->index;

	struct it87_data *data = dev_get_drvdata(dev);
	long val;

	if (strict_strtol(buf, 10, &val) < 0)
		return -EINVAL;

	mutex_lock(&data->update_lock);
	data->temp_low[nr] = TEMP_TO_REG(val);
	it87_write_value(data, IT87_REG_TEMP_LOW(nr), data->temp_low[nr]);
	mutex_unlock(&data->update_lock);
	return count;
}
#define show_temp_offset(offset)					\
static SENSOR_DEVICE_ATTR(temp##offset##_input, S_IRUGO,		\
		show_temp, NULL, offset - 1);				\
static SENSOR_DEVICE_ATTR(temp##offset##_max, S_IRUGO | S_IWUSR,	\
		show_temp_max, set_temp_max, offset - 1);		\
static SENSOR_DEVICE_ATTR(temp##offset##_min, S_IRUGO | S_IWUSR,	\
		show_temp_min, set_temp_min, offset - 1);

show_temp_offset(1);
show_temp_offset(2);
show_temp_offset(3);

static ssize_t show_sensor(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr);
	int nr = sensor_attr->index;

	struct it87_data *data = it87_update_device(dev);
	u8 reg = data->sensor;		/* In case the value is updated while
					   we use it */

	if (reg & (1 << nr))
		return sprintf(buf, "3\n");  /* thermal diode */
	if (reg & (8 << nr))
		return sprintf(buf, "4\n");  /* thermistor */
	return sprintf(buf, "0\n");      /* disabled */
}
static ssize_t set_sensor(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr);
	int nr = sensor_attr->index;

	struct it87_data *data = dev_get_drvdata(dev);
	long val;
	u8 reg;

	if (strict_strtol(buf, 10, &val) < 0)
		return -EINVAL;

	reg = it87_read_value(data, IT87_REG_TEMP_ENABLE);
	reg &= ~(1 << nr);
	reg &= ~(8 << nr);
	if (val == 2) {	/* backwards compatibility */
		dev_warn(dev, "Sensor type 2 is deprecated, please use 4 "
			 "instead\n");
		val = 4;
	}
	/* 3 = thermal diode; 4 = thermistor; 0 = disabled */
	if (val == 3)
		reg |= 1 << nr;
	else if (val == 4)
		reg |= 8 << nr;
	else if (val != 0)
		return -EINVAL;

	mutex_lock(&data->update_lock);
	data->sensor = reg;
	it87_write_value(data, IT87_REG_TEMP_ENABLE, data->sensor);
	data->valid = 0;	/* Force cache refresh */
	mutex_unlock(&data->update_lock);
	return count;
}
#define show_sensor_offset(offset)					\
static SENSOR_DEVICE_ATTR(temp##offset##_type, S_IRUGO | S_IWUSR,	\
		show_sensor, set_sensor, offset - 1);

show_sensor_offset(1);
show_sensor_offset(2);
show_sensor_offset(3);

/* 3 Fans */

static int pwm_mode(const struct it87_data *data, int nr)
{
	int ctrl = data->fan_main_ctrl & (1 << nr);

	if (ctrl == 0)					/* Full speed */
		return 0;
	if (data->pwm_ctrl[nr] & 0x80)			/* Automatic mode */
		return 2;
	else						/* Manual mode */
		return 1;
}

static ssize_t show_fan(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr);
	int nr = sensor_attr->index;

	struct it87_data *data = it87_update_device(dev);
	return sprintf(buf, "%d\n", FAN_FROM_REG(data->fan[nr],
				DIV_FROM_REG(data->fan_div[nr])));
}
static ssize_t show_fan_min(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr);
	int nr = sensor_attr->index;

	struct it87_data *data = it87_update_device(dev);
	return sprintf(buf, "%d\n", FAN_FROM_REG(data->fan_min[nr],
				DIV_FROM_REG(data->fan_div[nr])));
}
static ssize_t show_fan_div(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr);
	int nr = sensor_attr->index;

	struct it87_data *data = it87_update_device(dev);
	return sprintf(buf, "%d\n", DIV_FROM_REG(data->fan_div[nr]));
}
static ssize_t show_pwm_enable(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr);
	int nr = sensor_attr->index;

	struct it87_data *data = it87_update_device(dev);
	return sprintf(buf, "%d\n", pwm_mode(data, nr));
}
static ssize_t show_pwm(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr);
	int nr = sensor_attr->index;

	struct it87_data *data = it87_update_device(dev);
	return sprintf(buf, "%d\n",
		       pwm_from_reg(data, data->pwm_duty[nr]));
}
static ssize_t show_pwm_freq(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct it87_data *data = it87_update_device(dev);
	int index = (data->fan_ctl >> 4) & 0x07;

	return sprintf(buf, "%u\n", pwm_freq[index]);
}
static ssize_t set_fan_min(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr);
	int nr = sensor_attr->index;

	struct it87_data *data = dev_get_drvdata(dev);
	long val;
	u8 reg;

	if (strict_strtol(buf, 10, &val) < 0)
		return -EINVAL;

	mutex_lock(&data->update_lock);
	reg = it87_read_value(data, IT87_REG_FAN_DIV);
	switch (nr) {
	case 0:
		data->fan_div[nr] = reg & 0x07;
		break;
	case 1:
		data->fan_div[nr] = (reg >> 3) & 0x07;
		break;
	case 2:
		data->fan_div[nr] = (reg & 0x40) ? 3 : 1;
		break;
	}

	data->fan_min[nr] = FAN_TO_REG(val, DIV_FROM_REG(data->fan_div[nr]));
	it87_write_value(data, IT87_REG_FAN_MIN[nr], data->fan_min[nr]);
	mutex_unlock(&data->update_lock);
	return count;
}
static ssize_t set_fan_div(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr);
	int nr = sensor_attr->index;

	struct it87_data *data = dev_get_drvdata(dev);
	unsigned long val;
	int min;
	u8 old;

	if (strict_strtoul(buf, 10, &val) < 0)
		return -EINVAL;

	mutex_lock(&data->update_lock);
	old = it87_read_value(data, IT87_REG_FAN_DIV);

	/* Save fan min limit */
	min = FAN_FROM_REG(data->fan_min[nr], DIV_FROM_REG(data->fan_div[nr]));

	switch (nr) {
	case 0:
	case 1:
		data->fan_div[nr] = DIV_TO_REG(val);
		break;
	case 2:
		if (val < 8)
			data->fan_div[nr] = 1;
		else
			data->fan_div[nr] = 3;
	}
	val = old & 0x80;
	val |= (data->fan_div[0] & 0x07);
	val |= (data->fan_div[1] & 0x07) << 3;
	if (data->fan_div[2] == 3)
		val |= 0x1 << 6;
	it87_write_value(data, IT87_REG_FAN_DIV, val);

	/* Restore fan min limit */
	data->fan_min[nr] = FAN_TO_REG(min, DIV_FROM_REG(data->fan_div[nr]));
	it87_write_value(data, IT87_REG_FAN_MIN[nr], data->fan_min[nr]);

	mutex_unlock(&data->update_lock);
	return count;
}

/* Returns 0 if OK, -EINVAL otherwise */
static int check_trip_points(struct device *dev, int nr)
{
	const struct it87_data *data = dev_get_drvdata(dev);
	int i, err = 0;

	if (has_old_autopwm(data)) {
		for (i = 0; i < 3; i++) {
			if (data->auto_temp[nr][i] > data->auto_temp[nr][i + 1])
				err = -EINVAL;
		}
		for (i = 0; i < 2; i++) {
			if (data->auto_pwm[nr][i] > data->auto_pwm[nr][i + 1])
				err = -EINVAL;
		}
	}

	if (err) {
		dev_err(dev, "Inconsistent trip points, not switching to "
			"automatic mode\n");
		dev_err(dev, "Adjust the trip points and try again\n");
	}
	return err;
}

static ssize_t set_pwm_enable(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr);
	int nr = sensor_attr->index;

	struct it87_data *data = dev_get_drvdata(dev);
	long val;

	if (strict_strtol(buf, 10, &val) < 0 || val < 0 || val > 2)
		return -EINVAL;

	/* Check trip points before switching to automatic mode */
	if (val == 2) {
		if (check_trip_points(dev, nr) < 0)
			return -EINVAL;
	}

	mutex_lock(&data->update_lock);

	if (val == 0) {
		int tmp;
		/* make sure the fan is on when in on/off mode */
		tmp = it87_read_value(data, IT87_REG_FAN_CTL);
		it87_write_value(data, IT87_REG_FAN_CTL, tmp | (1 << nr));
		/* set on/off mode */
		data->fan_main_ctrl &= ~(1 << nr);
		it87_write_value(data, IT87_REG_FAN_MAIN_CTRL,
				 data->fan_main_ctrl);
	} else {
		if (val == 1)				/* Manual mode */
			data->pwm_ctrl[nr] = data->type == it8721 ?
					     data->pwm_temp_map[nr] :
					     data->pwm_duty[nr];
		else					/* Automatic mode */
			data->pwm_ctrl[nr] = 0x80 | data->pwm_temp_map[nr];
		it87_write_value(data, IT87_REG_PWM(nr), data->pwm_ctrl[nr]);
		/* set SmartGuardian mode */
		data->fan_main_ctrl |= (1 << nr);
		it87_write_value(data, IT87_REG_FAN_MAIN_CTRL,
				 data->fan_main_ctrl);
	}

	mutex_unlock(&data->update_lock);
	return count;
}
static ssize_t set_pwm(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr);
	int nr = sensor_attr->index;

	struct it87_data *data = dev_get_drvdata(dev);
	long val;

	if (strict_strtol(buf, 10, &val) < 0 || val < 0 || val > 255)
		return -EINVAL;

	mutex_lock(&data->update_lock);
	if (data->type == it8721) {
		/* If we are in automatic mode, the PWM duty cycle register
		 * is read-only so we can't write the value */
		if (data->pwm_ctrl[nr] & 0x80) {
			mutex_unlock(&data->update_lock);
			return -EBUSY;
		}
		data->pwm_duty[nr] = pwm_to_reg(data, val);
		it87_write_value(data, IT87_REG_PWM_DUTY(nr),
				 data->pwm_duty[nr]);
	} else {
		data->pwm_duty[nr] = pwm_to_reg(data, val);
		/* If we are in manual mode, write the duty cycle immediately;
		 * otherwise, just store it for later use. */
		if (!(data->pwm_ctrl[nr] & 0x80)) {
			data->pwm_ctrl[nr] = data->pwm_duty[nr];
			it87_write_value(data, IT87_REG_PWM(nr),
					 data->pwm_ctrl[nr]);
		}
	}
	mutex_unlock(&data->update_lock);
	return count;
}
static ssize_t set_pwm_freq(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct it87_data *data = dev_get_drvdata(dev);
	unsigned long val;
	int i;

	if (strict_strtoul(buf, 10, &val) < 0)
		return -EINVAL;

	/* Search for the nearest available frequency */
	for (i = 0; i < 7; i++) {
		if (val > (pwm_freq[i] + pwm_freq[i+1]) / 2)
			break;
	}

	mutex_lock(&data->update_lock);
	data->fan_ctl = it87_read_value(data, IT87_REG_FAN_CTL) & 0x8f;
	data->fan_ctl |= i << 4;
	it87_write_value(data, IT87_REG_FAN_CTL, data->fan_ctl);
	mutex_unlock(&data->update_lock);

	return count;
}
static ssize_t show_pwm_temp_map(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr);
	int nr = sensor_attr->index;

	struct it87_data *data = it87_update_device(dev);
	int map;

	if (data->pwm_temp_map[nr] < 3)
		map = 1 << data->pwm_temp_map[nr];
	else
		map = 0;			/* Should never happen */
	return sprintf(buf, "%d\n", map);
}
static ssize_t set_pwm_temp_map(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr);
	int nr = sensor_attr->index;

	struct it87_data *data = dev_get_drvdata(dev);
	long val;
	u8 reg;

	/* This check can go away if we ever support automatic fan speed
	   control on newer chips. */
	if (!has_old_autopwm(data)) {
		dev_notice(dev, "Mapping change disabled for safety reasons\n");
		return -EINVAL;
	}

	if (strict_strtol(buf, 10, &val) < 0)
		return -EINVAL;

	switch (val) {
	case (1 << 0):
		reg = 0x00;
		break;
	case (1 << 1):
		reg = 0x01;
		break;
	case (1 << 2):
		reg = 0x02;
		break;
	default:
		return -EINVAL;
	}

	mutex_lock(&data->update_lock);
	data->pwm_temp_map[nr] = reg;
	/* If we are in automatic mode, write the temp mapping immediately;
	 * otherwise, just store it for later use. */
	if (data->pwm_ctrl[nr] & 0x80) {
		data->pwm_ctrl[nr] = 0x80 | data->pwm_temp_map[nr];
		it87_write_value(data, IT87_REG_PWM(nr), data->pwm_ctrl[nr]);
	}
	mutex_unlock(&data->update_lock);
	return count;
}

static ssize_t show_auto_pwm(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct it87_data *data = it87_update_device(dev);
	struct sensor_device_attribute_2 *sensor_attr =
			to_sensor_dev_attr_2(attr);
	int nr = sensor_attr->nr;
	int point = sensor_attr->index;

	return sprintf(buf, "%d\n",
		       pwm_from_reg(data, data->auto_pwm[nr][point]));
}

static ssize_t set_auto_pwm(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct it87_data *data = dev_get_drvdata(dev);
	struct sensor_device_attribute_2 *sensor_attr =
			to_sensor_dev_attr_2(attr);
	int nr = sensor_attr->nr;
	int point = sensor_attr->index;
	long val;

	if (strict_strtol(buf, 10, &val) < 0 || val < 0 || val > 255)
		return -EINVAL;

	mutex_lock(&data->update_lock);
	data->auto_pwm[nr][point] = pwm_to_reg(data, val);
	it87_write_value(data, IT87_REG_AUTO_PWM(nr, point),
			 data->auto_pwm[nr][point]);
	mutex_unlock(&data->update_lock);
	return count;
}

static ssize_t show_auto_temp(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct it87_data *data = it87_update_device(dev);
	struct sensor_device_attribute_2 *sensor_attr =
			to_sensor_dev_attr_2(attr);
	int nr = sensor_attr->nr;
	int point = sensor_attr->index;

	return sprintf(buf, "%d\n", TEMP_FROM_REG(data->auto_temp[nr][point]));
}

static ssize_t set_auto_temp(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct it87_data *data = dev_get_drvdata(dev);
	struct sensor_device_attribute_2 *sensor_attr =
			to_sensor_dev_attr_2(attr);
	int nr = sensor_attr->nr;
	int point = sensor_attr->index;
	long val;

	if (strict_strtol(buf, 10, &val) < 0 || val < -128000 || val > 127000)
		return -EINVAL;

	mutex_lock(&data->update_lock);
	data->auto_temp[nr][point] = TEMP_TO_REG(val);
	it87_write_value(data, IT87_REG_AUTO_TEMP(nr, point),
			 data->auto_temp[nr][point]);
	mutex_unlock(&data->update_lock);
	return count;
}

#define show_fan_offset(offset)					\
static SENSOR_DEVICE_ATTR(fan##offset##_input, S_IRUGO,		\
		show_fan, NULL, offset - 1);			\
static SENSOR_DEVICE_ATTR(fan##offset##_min, S_IRUGO | S_IWUSR, \
		show_fan_min, set_fan_min, offset - 1);		\
static SENSOR_DEVICE_ATTR(fan##offset##_div, S_IRUGO | S_IWUSR, \
		show_fan_div, set_fan_div, offset - 1);

show_fan_offset(1);
show_fan_offset(2);
show_fan_offset(3);

#define show_pwm_offset(offset)						\
static SENSOR_DEVICE_ATTR(pwm##offset##_enable, S_IRUGO | S_IWUSR,	\
		show_pwm_enable, set_pwm_enable, offset - 1);		\
static SENSOR_DEVICE_ATTR(pwm##offset, S_IRUGO | S_IWUSR,		\
		show_pwm, set_pwm, offset - 1);				\
static DEVICE_ATTR(pwm##offset##_freq,					\
		(offset == 1 ? S_IRUGO | S_IWUSR : S_IRUGO),		\
		show_pwm_freq, (offset == 1 ? set_pwm_freq : NULL));	\
static SENSOR_DEVICE_ATTR(pwm##offset##_auto_channels_temp,		\
		S_IRUGO | S_IWUSR, show_pwm_temp_map, set_pwm_temp_map,	\
		offset - 1);						\
static SENSOR_DEVICE_ATTR_2(pwm##offset##_auto_point1_pwm,		\
		S_IRUGO | S_IWUSR, show_auto_pwm, set_auto_pwm,		\
		offset - 1, 0);						\
static SENSOR_DEVICE_ATTR_2(pwm##offset##_auto_point2_pwm,		\
		S_IRUGO | S_IWUSR, show_auto_pwm, set_auto_pwm,		\
		offset - 1, 1);						\
static SENSOR_DEVICE_ATTR_2(pwm##offset##_auto_point3_pwm,		\
		S_IRUGO | S_IWUSR, show_auto_pwm, set_auto_pwm,		\
		offset - 1, 2);						\
static SENSOR_DEVICE_ATTR_2(pwm##offset##_auto_point4_pwm,		\
		S_IRUGO, show_auto_pwm, NULL, offset - 1, 3);		\
static SENSOR_DEVICE_ATTR_2(pwm##offset##_auto_point1_temp,		\
		S_IRUGO | S_IWUSR, show_auto_temp, set_auto_temp,	\
		offset - 1, 1);						\
static SENSOR_DEVICE_ATTR_2(pwm##offset##_auto_point1_temp_hyst,	\
		S_IRUGO | S_IWUSR, show_auto_temp, set_auto_temp,	\
		offset - 1, 0);						\
static SENSOR_DEVICE_ATTR_2(pwm##offset##_auto_point2_temp,		\
		S_IRUGO | S_IWUSR, show_auto_temp, set_auto_temp,	\
		offset - 1, 2);						\
static SENSOR_DEVICE_ATTR_2(pwm##offset##_auto_point3_temp,		\
		S_IRUGO | S_IWUSR, show_auto_temp, set_auto_temp,	\
		offset - 1, 3);						\
static SENSOR_DEVICE_ATTR_2(pwm##offset##_auto_point4_temp,		\
		S_IRUGO | S_IWUSR, show_auto_temp, set_auto_temp,	\
		offset - 1, 4);

show_pwm_offset(1);
show_pwm_offset(2);
show_pwm_offset(3);

/* A different set of callbacks for 16-bit fans */
static ssize_t show_fan16(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr);
	int nr = sensor_attr->index;
	struct it87_data *data = it87_update_device(dev);
	return sprintf(buf, "%d\n", FAN16_FROM_REG(data->fan[nr]));
}

static ssize_t show_fan16_min(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr);
	int nr = sensor_attr->index;
	struct it87_data *data = it87_update_device(dev);
	return sprintf(buf, "%d\n", FAN16_FROM_REG(data->fan_min[nr]));
}

static ssize_t set_fan16_min(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr);
	int nr = sensor_attr->index;
	struct it87_data *data = dev_get_drvdata(dev);
	long val;

	if (strict_strtol(buf, 10, &val) < 0)
		return -EINVAL;

	mutex_lock(&data->update_lock);
	data->fan_min[nr] = FAN16_TO_REG(val);
	it87_write_value(data, IT87_REG_FAN_MIN[nr],
			 data->fan_min[nr] & 0xff);
	it87_write_value(data, IT87_REG_FANX_MIN[nr],
			 data->fan_min[nr] >> 8);
	mutex_unlock(&data->update_lock);
	return count;
}

/* We want to use the same sysfs file names as 8-bit fans, but we need
   different variable names, so we have to use SENSOR_ATTR instead of
   SENSOR_DEVICE_ATTR. */
#define show_fan16_offset(offset) \
static struct sensor_device_attribute sensor_dev_attr_fan##offset##_input16 \
	= SENSOR_ATTR(fan##offset##_input, S_IRUGO,		\
		show_fan16, NULL, offset - 1);			\
static struct sensor_device_attribute sensor_dev_attr_fan##offset##_min16 \
	= SENSOR_ATTR(fan##offset##_min, S_IRUGO | S_IWUSR,	\
		show_fan16_min, set_fan16_min, offset - 1)

show_fan16_offset(1);
show_fan16_offset(2);
show_fan16_offset(3);
show_fan16_offset(4);
show_fan16_offset(5);

/* Alarms */
static ssize_t show_alarms(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct it87_data *data = it87_update_device(dev);
	return sprintf(buf, "%u\n", data->alarms);
}
static DEVICE_ATTR(alarms, S_IRUGO, show_alarms, NULL);

static ssize_t show_alarm(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	int bitnr = to_sensor_dev_attr(attr)->index;
	struct it87_data *data = it87_update_device(dev);
	return sprintf(buf, "%u\n", (data->alarms >> bitnr) & 1);
}
static SENSOR_DEVICE_ATTR(in0_alarm, S_IRUGO, show_alarm, NULL, 8);
static SENSOR_DEVICE_ATTR(in1_alarm, S_IRUGO, show_alarm, NULL, 9);
static SENSOR_DEVICE_ATTR(in2_alarm, S_IRUGO, show_alarm, NULL, 10);
static SENSOR_DEVICE_ATTR(in3_alarm, S_IRUGO, show_alarm, NULL, 11);
static SENSOR_DEVICE_ATTR(in4_alarm, S_IRUGO, show_alarm, NULL, 12);
static SENSOR_DEVICE_ATTR(in5_alarm, S_IRUGO, show_alarm, NULL, 13);
static SENSOR_DEVICE_ATTR(in6_alarm, S_IRUGO, show_alarm, NULL, 14);
static SENSOR_DEVICE_ATTR(in7_alarm, S_IRUGO, show_alarm, NULL, 15);
static SENSOR_DEVICE_ATTR(fan1_alarm, S_IRUGO, show_alarm, NULL, 0);
static SENSOR_DEVICE_ATTR(fan2_alarm, S_IRUGO, show_alarm, NULL, 1);
static SENSOR_DEVICE_ATTR(fan3_alarm, S_IRUGO, show_alarm, NULL, 2);
static SENSOR_DEVICE_ATTR(fan4_alarm, S_IRUGO, show_alarm, NULL, 3);
static SENSOR_DEVICE_ATTR(fan5_alarm, S_IRUGO, show_alarm, NULL, 6);
static SENSOR_DEVICE_ATTR(temp1_alarm, S_IRUGO, show_alarm, NULL, 16);
static SENSOR_DEVICE_ATTR(temp2_alarm, S_IRUGO, show_alarm, NULL, 17);
static SENSOR_DEVICE_ATTR(temp3_alarm, S_IRUGO, show_alarm, NULL, 18);

static ssize_t show_beep(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	int bitnr = to_sensor_dev_attr(attr)->index;
	struct it87_data *data = it87_update_device(dev);
	return sprintf(buf, "%u\n", (data->beeps >> bitnr) & 1);
}
static ssize_t set_beep(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	int bitnr = to_sensor_dev_attr(attr)->index;
	struct it87_data *data = dev_get_drvdata(dev);
	long val;

	if (strict_strtol(buf, 10, &val) < 0
	 || (val != 0 && val != 1))
		return -EINVAL;

	mutex_lock(&data->update_lock);
	data->beeps = it87_read_value(data, IT87_REG_BEEP_ENABLE);
	if (val)
		data->beeps |= (1 << bitnr);
	else
		data->beeps &= ~(1 << bitnr);
	it87_write_value(data, IT87_REG_BEEP_ENABLE, data->beeps);
	mutex_unlock(&data->update_lock);
	return count;
}

static SENSOR_DEVICE_ATTR(in0_beep, S_IRUGO | S_IWUSR,
			  show_beep, set_beep, 1);
static SENSOR_DEVICE_ATTR(in1_beep, S_IRUGO, show_beep, NULL, 1);
static SENSOR_DEVICE_ATTR(in2_beep, S_IRUGO, show_beep, NULL, 1);
static SENSOR_DEVICE_ATTR(in3_beep, S_IRUGO, show_beep, NULL, 1);
static SENSOR_DEVICE_ATTR(in4_beep, S_IRUGO, show_beep, NULL, 1);
static SENSOR_DEVICE_ATTR(in5_beep, S_IRUGO, show_beep, NULL, 1);
static SENSOR_DEVICE_ATTR(in6_beep, S_IRUGO, show_beep, NULL, 1);
static SENSOR_DEVICE_ATTR(in7_beep, S_IRUGO, show_beep, NULL, 1);
/* fanX_beep writability is set later */
static SENSOR_DEVICE_ATTR(fan1_beep, S_IRUGO, show_beep, set_beep, 0);
static SENSOR_DEVICE_ATTR(fan2_beep, S_IRUGO, show_beep, set_beep, 0);
static SENSOR_DEVICE_ATTR(fan3_beep, S_IRUGO, show_beep, set_beep, 0);
static SENSOR_DEVICE_ATTR(fan4_beep, S_IRUGO, show_beep, set_beep, 0);
static SENSOR_DEVICE_ATTR(fan5_beep, S_IRUGO, show_beep, set_beep, 0);
static SENSOR_DEVICE_ATTR(temp1_beep, S_IRUGO | S_IWUSR,
			  show_beep, set_beep, 2);
static SENSOR_DEVICE_ATTR(temp2_beep, S_IRUGO, show_beep, NULL, 2);
static SENSOR_DEVICE_ATTR(temp3_beep, S_IRUGO, show_beep, NULL, 2);

static ssize_t show_vrm_reg(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct it87_data *data = dev_get_drvdata(dev);
	return sprintf(buf, "%u\n", data->vrm);
}
static ssize_t store_vrm_reg(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct it87_data *data = dev_get_drvdata(dev);
	unsigned long val;

	if (strict_strtoul(buf, 10, &val) < 0)
		return -EINVAL;

	data->vrm = val;

	return count;
}
static DEVICE_ATTR(vrm, S_IRUGO | S_IWUSR, show_vrm_reg, store_vrm_reg);

static ssize_t show_vid_reg(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct it87_data *data = it87_update_device(dev);
	return sprintf(buf, "%ld\n", (long) vid_from_reg(data->vid, data->vrm));
}
static DEVICE_ATTR(cpu0_vid, S_IRUGO, show_vid_reg, NULL);

static ssize_t show_label(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	static const char *labels[] = {
		"+5V",
		"5VSB",
		"Vbat",
	};
	static const char *labels_it8721[] = {
		"+3.3V",
		"3VSB",
		"Vbat",
	};
	struct it87_data *data = dev_get_drvdata(dev);
	int nr = to_sensor_dev_attr(attr)->index;

	return sprintf(buf, "%s\n", data->type == it8721 ? labels_it8721[nr]
							 : labels[nr]);
}
static SENSOR_DEVICE_ATTR(in3_label, S_IRUGO, show_label, NULL, 0);
static SENSOR_DEVICE_ATTR(in7_label, S_IRUGO, show_label, NULL, 1);
static SENSOR_DEVICE_ATTR(in8_label, S_IRUGO, show_label, NULL, 2);

static ssize_t show_name(struct device *dev, struct device_attribute
			 *devattr, char *buf)
{
	struct it87_data *data = dev_get_drvdata(dev);
	return sprintf(buf, "%s\n", data->name);
}
static DEVICE_ATTR(name, S_IRUGO, show_name, NULL);

static struct attribute *it87_attributes[] = {
	&sensor_dev_attr_in0_input.dev_attr.attr,
	&sensor_dev_attr_in1_input.dev_attr.attr,
	&sensor_dev_attr_in2_input.dev_attr.attr,
	&sensor_dev_attr_in3_input.dev_attr.attr,
	&sensor_dev_attr_in4_input.dev_attr.attr,
	&sensor_dev_attr_in5_input.dev_attr.attr,
	&sensor_dev_attr_in6_input.dev_attr.attr,
	&sensor_dev_attr_in7_input.dev_attr.attr,
	&sensor_dev_attr_in8_input.dev_attr.attr,
	&sensor_dev_attr_in0_min.dev_attr.attr,
	&sensor_dev_attr_in1_min.dev_attr.attr,
	&sensor_dev_attr_in2_min.dev_attr.attr,
	&sensor_dev_attr_in3_min.dev_attr.attr,
	&sensor_dev_attr_in4_min.dev_attr.attr,
	&sensor_dev_attr_in5_min.dev_attr.attr,
	&sensor_dev_attr_in6_min.dev_attr.attr,
	&sensor_dev_attr_in7_min.dev_attr.attr,
	&sensor_dev_attr_in0_max.dev_attr.attr,
	&sensor_dev_attr_in1_max.dev_attr.attr,
	&sensor_dev_attr_in2_max.dev_attr.attr,
	&sensor_dev_attr_in3_max.dev_attr.attr,
	&sensor_dev_attr_in4_max.dev_attr.attr,
	&sensor_dev_attr_in5_max.dev_attr.attr,
	&sensor_dev_attr_in6_max.dev_attr.attr,
	&sensor_dev_attr_in7_max.dev_attr.attr,
	&sensor_dev_attr_in0_alarm.dev_attr.attr,
	&sensor_dev_attr_in1_alarm.dev_attr.attr,
	&sensor_dev_attr_in2_alarm.dev_attr.attr,
	&sensor_dev_attr_in3_alarm.dev_attr.attr,
	&sensor_dev_attr_in4_alarm.dev_attr.attr,
	&sensor_dev_attr_in5_alarm.dev_attr.attr,
	&sensor_dev_attr_in6_alarm.dev_attr.attr,
	&sensor_dev_attr_in7_alarm.dev_attr.attr,

	&sensor_dev_attr_temp1_input.dev_attr.attr,
	&sensor_dev_attr_temp2_input.dev_attr.attr,
	&sensor_dev_attr_temp3_input.dev_attr.attr,
	&sensor_dev_attr_temp1_max.dev_attr.attr,
	&sensor_dev_attr_temp2_max.dev_attr.attr,
	&sensor_dev_attr_temp3_max.dev_attr.attr,
	&sensor_dev_attr_temp1_min.dev_attr.attr,
	&sensor_dev_attr_temp2_min.dev_attr.attr,
	&sensor_dev_attr_temp3_min.dev_attr.attr,
	&sensor_dev_attr_temp1_type.dev_attr.attr,
	&sensor_dev_attr_temp2_type.dev_attr.attr,
	&sensor_dev_attr_temp3_type.dev_attr.attr,
	&sensor_dev_attr_temp1_alarm.dev_attr.attr,
	&sensor_dev_attr_temp2_alarm.dev_attr.attr,
	&sensor_dev_attr_temp3_alarm.dev_attr.attr,

	&dev_attr_alarms.attr,
	&dev_attr_name.attr,
	NULL
};

static const struct attribute_group it87_group = {
	.attrs = it87_attributes,
};

static struct attribute *it87_attributes_beep[] = {
	&sensor_dev_attr_in0_beep.dev_attr.attr,
	&sensor_dev_attr_in1_beep.dev_attr.attr,
	&sensor_dev_attr_in2_beep.dev_attr.attr,
	&sensor_dev_attr_in3_beep.dev_attr.attr,
	&sensor_dev_attr_in4_beep.dev_attr.attr,
	&sensor_dev_attr_in5_beep.dev_attr.attr,
	&sensor_dev_attr_in6_beep.dev_attr.attr,
	&sensor_dev_attr_in7_beep.dev_attr.attr,

	&sensor_dev_attr_temp1_beep.dev_attr.attr,
	&sensor_dev_attr_temp2_beep.dev_attr.attr,
	&sensor_dev_attr_temp3_beep.dev_attr.attr,
	NULL
};

static const struct attribute_group it87_group_beep = {
	.attrs = it87_attributes_beep,
};

static struct attribute *it87_attributes_fan16[5][3+1] = { {
	&sensor_dev_attr_fan1_input16.dev_attr.attr,
	&sensor_dev_attr_fan1_min16.dev_attr.attr,
	&sensor_dev_attr_fan1_alarm.dev_attr.attr,
	NULL
}, {
	&sensor_dev_attr_fan2_input16.dev_attr.attr,
	&sensor_dev_attr_fan2_min16.dev_attr.attr,
	&sensor_dev_attr_fan2_alarm.dev_attr.attr,
	NULL
}, {
	&sensor_dev_attr_fan3_input16.dev_attr.attr,
	&sensor_dev_attr_fan3_min16.dev_attr.attr,
	&sensor_dev_attr_fan3_alarm.dev_attr.attr,
	NULL
}, {
	&sensor_dev_attr_fan4_input16.dev_attr.attr,
	&sensor_dev_attr_fan4_min16.dev_attr.attr,
	&sensor_dev_attr_fan4_alarm.dev_attr.attr,
	NULL
}, {
	&sensor_dev_attr_fan5_input16.dev_attr.attr,
	&sensor_dev_attr_fan5_min16.dev_attr.attr,
	&sensor_dev_attr_fan5_alarm.dev_attr.attr,
	NULL
} };

static const struct attribute_group it87_group_fan16[5] = {
	{ .attrs = it87_attributes_fan16[0] },
	{ .attrs = it87_attributes_fan16[1] },
	{ .attrs = it87_attributes_fan16[2] },
	{ .attrs = it87_attributes_fan16[3] },
	{ .attrs = it87_attributes_fan16[4] },
};

static struct attribute *it87_attributes_fan[3][4+1] = { {
	&sensor_dev_attr_fan1_input.dev_attr.attr,
	&sensor_dev_attr_fan1_min.dev_attr.attr,
	&sensor_dev_attr_fan1_div.dev_attr.attr,
	&sensor_dev_attr_fan1_alarm.dev_attr.attr,
	NULL
}, {
	&sensor_dev_attr_fan2_input.dev_attr.attr,
	&sensor_dev_attr_fan2_min.dev_attr.attr,
	&sensor_dev_attr_fan2_div.dev_attr.attr,
	&sensor_dev_attr_fan2_alarm.dev_attr.attr,
	NULL
}, {
	&sensor_dev_attr_fan3_input.dev_attr.attr,
	&sensor_dev_attr_fan3_min.dev_attr.attr,
	&sensor_dev_attr_fan3_div.dev_attr.attr,
	&sensor_dev_attr_fan3_alarm.dev_attr.attr,
	NULL
} };

static const struct attribute_group it87_group_fan[3] = {
	{ .attrs = it87_attributes_fan[0] },
	{ .attrs = it87_attributes_fan[1] },
	{ .attrs = it87_attributes_fan[2] },
};

static const struct attribute_group *
it87_get_fan_group(const struct it87_data *data)
{
	return has_16bit_fans(data) ? it87_group_fan16 : it87_group_fan;
}

static struct attribute *it87_attributes_pwm[3][4+1] = { {
	&sensor_dev_attr_pwm1_enable.dev_attr.attr,
	&sensor_dev_attr_pwm1.dev_attr.attr,
	&dev_attr_pwm1_freq.attr,
	&sensor_dev_attr_pwm1_auto_channels_temp.dev_attr.attr,
	NULL
}, {
	&sensor_dev_attr_pwm2_enable.dev_attr.attr,
	&sensor_dev_attr_pwm2.dev_attr.attr,
	&dev_attr_pwm2_freq.attr,
	&sensor_dev_attr_pwm2_auto_channels_temp.dev_attr.attr,
	NULL
}, {
	&sensor_dev_attr_pwm3_enable.dev_attr.attr,
	&sensor_dev_attr_pwm3.dev_attr.attr,
	&dev_attr_pwm3_freq.attr,
	&sensor_dev_attr_pwm3_auto_channels_temp.dev_attr.attr,
	NULL
} };

static const struct attribute_group it87_group_pwm[3] = {
	{ .attrs = it87_attributes_pwm[0] },
	{ .attrs = it87_attributes_pwm[1] },
	{ .attrs = it87_attributes_pwm[2] },
};

static struct attribute *it87_attributes_autopwm[3][9+1] = { {
	&sensor_dev_attr_pwm1_auto_point1_pwm.dev_attr.attr,
	&sensor_dev_attr_pwm1_auto_point2_pwm.dev_attr.attr,
	&sensor_dev_attr_pwm1_auto_point3_pwm.dev_attr.attr,
	&sensor_dev_attr_pwm1_auto_point4_pwm.dev_attr.attr,
	&sensor_dev_attr_pwm1_auto_point1_temp.dev_attr.attr,
	&sensor_dev_attr_pwm1_auto_point1_temp_hyst.dev_attr.attr,
	&sensor_dev_attr_pwm1_auto_point2_temp.dev_attr.attr,
	&sensor_dev_attr_pwm1_auto_point3_temp.dev_attr.attr,
	&sensor_dev_attr_pwm1_auto_point4_temp.dev_attr.attr,
	NULL
}, {
	&sensor_dev_attr_pwm2_auto_point1_pwm.dev_attr.attr,
	&sensor_dev_attr_pwm2_auto_point2_pwm.dev_attr.attr,
	&sensor_dev_attr_pwm2_auto_point3_pwm.dev_attr.attr,
	&sensor_dev_attr_pwm2_auto_point4_pwm.dev_attr.attr,
	&sensor_dev_attr_pwm2_auto_point1_temp.dev_attr.attr,
	&sensor_dev_attr_pwm2_auto_point1_temp_hyst.dev_attr.attr,
	&sensor_dev_attr_pwm2_auto_point2_temp.dev_attr.attr,
	&sensor_dev_attr_pwm2_auto_point3_temp.dev_attr.attr,
	&sensor_dev_attr_pwm2_auto_point4_temp.dev_attr.attr,
	NULL
}, {
	&sensor_dev_attr_pwm3_auto_point1_pwm.dev_attr.attr,
	&sensor_dev_attr_pwm3_auto_point2_pwm.dev_attr.attr,
	&sensor_dev_attr_pwm3_auto_point3_pwm.dev_attr.attr,
	&sensor_dev_attr_pwm3_auto_point4_pwm.dev_attr.attr,
	&sensor_dev_attr_pwm3_auto_point1_temp.dev_attr.attr,
	&sensor_dev_attr_pwm3_auto_point1_temp_hyst.dev_attr.attr,
	&sensor_dev_attr_pwm3_auto_point2_temp.dev_attr.attr,
	&sensor_dev_attr_pwm3_auto_point3_temp.dev_attr.attr,
	&sensor_dev_attr_pwm3_auto_point4_temp.dev_attr.attr,
	NULL
} };

static const struct attribute_group it87_group_autopwm[3] = {
	{ .attrs = it87_attributes_autopwm[0] },
	{ .attrs = it87_attributes_autopwm[1] },
	{ .attrs = it87_attributes_autopwm[2] },
};

static struct attribute *it87_attributes_fan_beep[] = {
	&sensor_dev_attr_fan1_beep.dev_attr.attr,
	&sensor_dev_attr_fan2_beep.dev_attr.attr,
	&sensor_dev_attr_fan3_beep.dev_attr.attr,
	&sensor_dev_attr_fan4_beep.dev_attr.attr,
	&sensor_dev_attr_fan5_beep.dev_attr.attr,
};

static struct attribute *it87_attributes_vid[] = {
	&dev_attr_vrm.attr,
	&dev_attr_cpu0_vid.attr,
	NULL
};

static const struct attribute_group it87_group_vid = {
	.attrs = it87_attributes_vid,
};

static struct attribute *it87_attributes_label[] = {
	&sensor_dev_attr_in3_label.dev_attr.attr,
	&sensor_dev_attr_in7_label.dev_attr.attr,
	&sensor_dev_attr_in8_label.dev_attr.attr,
	NULL
};

static const struct attribute_group it87_group_label = {
	.attrs = it87_attributes_label,
};

/* SuperIO detection - will change isa_address if a chip is found */
static int __init it87_find(unsigned short *address,
	struct it87_sio_data *sio_data)
{
	int err;
	u16 chip_type;
	const char *board_vendor, *board_name;

	err = superio_enter();
	if (err)
		return err;

	err = -ENODEV;
	chip_type = force_id ? force_id : superio_inw(DEVID);

	switch (chip_type) {
	case IT8705F_DEVID:
		sio_data->type = it87;
		break;
	case IT8712F_DEVID:
		sio_data->type = it8712;
		break;
	case IT8716F_DEVID:
	case IT8726F_DEVID:
		sio_data->type = it8716;
		break;
	case IT8718F_DEVID:
		sio_data->type = it8718;
		break;
	case IT8720F_DEVID:
		sio_data->type = it8720;
		break;
	case IT8721F_DEVID:
		sio_data->type = it8721;
		break;
	case 0xffff:	/* No device at all */
		goto exit;
	default:
		pr_debug("Unsupported chip (DEVID=0x%x)\n", chip_type);
		goto exit;
	}

	superio_select(PME);
	if (!(superio_inb(IT87_ACT_REG) & 0x01)) {
		pr_info("Device not activated, skipping\n");
		goto exit;
	}

	*address = superio_inw(IT87_BASE_REG) & ~(IT87_EXTENT - 1);
	if (*address == 0) {
		pr_info("Base address not set, skipping\n");
		goto exit;
	}

	err = 0;
	sio_data->revision = superio_inb(DEVREV) & 0x0f;
	pr_info("Found IT%04xF chip at 0x%x, revision %d\n",
		chip_type, *address, sio_data->revision);

	/* in8 (Vbat) is always internal */
	sio_data->internal = (1 << 2);

	/* Read GPIO config and VID value from LDN 7 (GPIO) */
	if (sio_data->type == it87) {
		/* The IT8705F doesn't have VID pins at all */
		sio_data->skip_vid = 1;

		/* The IT8705F has a different LD number for GPIO */
		superio_select(5);
		sio_data->beep_pin = superio_inb(IT87_SIO_BEEP_PIN_REG) & 0x3f;
	} else {
		int reg;

		superio_select(GPIO);

		reg = superio_inb(IT87_SIO_GPIO3_REG);
		if (sio_data->type == it8721) {
			/* The IT8721F/IT8758E doesn't have VID pins at all */
			sio_data->skip_vid = 1;
		} else {
			/* We need at least 4 VID pins */
			if (reg & 0x0f) {
				pr_info("VID is disabled (pins used for GPIO)\n");
				sio_data->skip_vid = 1;
			}
		}

		/* Check if fan3 is there or not */
		if (reg & (1 << 6))
			sio_data->skip_pwm |= (1 << 2);
		if (reg & (1 << 7))
			sio_data->skip_fan |= (1 << 2);

		/* Check if fan2 is there or not */
		reg = superio_inb(IT87_SIO_GPIO5_REG);
		if (reg & (1 << 1))
			sio_data->skip_pwm |= (1 << 1);
		if (reg & (1 << 2))
			sio_data->skip_fan |= (1 << 1);

		if ((sio_data->type == it8718 || sio_data->type == it8720)
		 && !(sio_data->skip_vid))
			sio_data->vid_value = superio_inb(IT87_SIO_VID_REG);

		reg = superio_inb(IT87_SIO_PINX2_REG);
		/*
		 * The IT8720F has no VIN7 pin, so VCCH should always be
		 * routed internally to VIN7 with an internal divider.
		 * Curiously, there still is a configuration bit to control
		 * this, which means it can be set incorrectly. And even
		 * more curiously, many boards out there are improperly
		 * configured, even though the IT8720F datasheet claims
		 * that the internal routing of VCCH to VIN7 is the default
		 * setting. So we force the internal routing in this case.
		 */
		if (sio_data->type == it8720 && !(reg & (1 << 1))) {
			reg |= (1 << 1);
			superio_outb(IT87_SIO_PINX2_REG, reg);
			pr_notice("Routing internal VCCH to in7\n");
		}
		if (reg & (1 << 0))
			sio_data->internal |= (1 << 0);
		if ((reg & (1 << 1)) || sio_data->type == it8721)
			sio_data->internal |= (1 << 1);

		sio_data->beep_pin = superio_inb(IT87_SIO_BEEP_PIN_REG) & 0x3f;
	}
	if (sio_data->beep_pin)
		pr_info("Beeping is supported\n");

	/* Disable specific features based on DMI strings */
	board_vendor = dmi_get_system_info(DMI_BOARD_VENDOR);
	board_name = dmi_get_system_info(DMI_BOARD_NAME);
	if (board_vendor && board_name) {
		if (strcmp(board_vendor, "nVIDIA") == 0
		 && strcmp(board_name, "FN68PT") == 0) {
			/* On the Shuttle SN68PT, FAN_CTL2 is apparently not
			   connected to a fan, but to something else. One user
			   has reported instant system power-off when changing
			   the PWM2 duty cycle, so we disable it.
			   I use the board name string as the trigger in case
			   the same board is ever used in other systems. */
			pr_info("Disabling pwm2 due to hardware constraints\n");
			sio_data->skip_pwm = (1 << 1);
		}
	}

exit:
	superio_exit();
	return err;
}

static void it87_remove_files(struct device *dev)
{
	struct it87_data *data = platform_get_drvdata(pdev);
	struct it87_sio_data *sio_data = dev->platform_data;
	const struct attribute_group *fan_group = it87_get_fan_group(data);
	int i;

	sysfs_remove_group(&dev->kobj, &it87_group);
	if (sio_data->beep_pin)
		sysfs_remove_group(&dev->kobj, &it87_group_beep);
	for (i = 0; i < 5; i++) {
		if (!(data->has_fan & (1 << i)))
			continue;
		sysfs_remove_group(&dev->kobj, &fan_group[i]);
		if (sio_data->beep_pin)
			sysfs_remove_file(&dev->kobj,
					  it87_attributes_fan_beep[i]);
	}
	for (i = 0; i < 3; i++) {
		if (sio_data->skip_pwm & (1 << 0))
			continue;
		sysfs_remove_group(&dev->kobj, &it87_group_pwm[i]);
		if (has_old_autopwm(data))
			sysfs_remove_group(&dev->kobj,
					   &it87_group_autopwm[i]);
	}
	if (!sio_data->skip_vid)
		sysfs_remove_group(&dev->kobj, &it87_group_vid);
	sysfs_remove_group(&dev->kobj, &it87_group_label);
}

static int __devinit it87_probe(struct platform_device *pdev)
{
	struct it87_data *data;
	struct resource *res;
	struct device *dev = &pdev->dev;
	struct it87_sio_data *sio_data = dev->platform_data;
	const struct attribute_group *fan_group;
	int err = 0, i;
	int enable_pwm_interface;
	int fan_beep_need_rw;
	static const char *names[] = {
		"it87",
		"it8712",
		"it8716",
		"it8718",
		"it8720",
		"it8721",
	};

	res = platform_get_resource(pdev, IORESOURCE_IO, 0);
	if (!request_region(res->start, IT87_EC_EXTENT, DRVNAME)) {
		dev_err(dev, "Failed to request region 0x%lx-0x%lx\n",
			(unsigned long)res->start,
			(unsigned long)(res->start + IT87_EC_EXTENT - 1));
		err = -EBUSY;
		goto ERROR0;
	}

	data = kzalloc(sizeof(struct it87_data), GFP_KERNEL);
	if (!data) {
		err = -ENOMEM;
		goto ERROR1;
	}

	data->addr = res->start;
	data->type = sio_data->type;
	data->revision = sio_data->revision;
	data->name = names[sio_data->type];

	/* Now, we do the remaining detection. */
	if ((it87_read_value(data, IT87_REG_CONFIG) & 0x80)
	 || it87_read_value(data, IT87_REG_CHIPID) != 0x90) {
		err = -ENODEV;
		goto ERROR2;
	}

	platform_set_drvdata(pdev, data);

	mutex_init(&data->update_lock);

	/* Check PWM configuration */
	enable_pwm_interface = it87_check_pwm(dev);

	/* Starting with IT8721F, we handle scaling of internal voltages */
	if (data->type == it8721) {
		if (sio_data->internal & (1 << 0))
			data->in_scaled |= (1 << 3);	/* in3 is AVCC */
		if (sio_data->internal & (1 << 1))
			data->in_scaled |= (1 << 7);	/* in7 is VSB */
		if (sio_data->internal & (1 << 2))
			data->in_scaled |= (1 << 8);	/* in8 is Vbat */
	}

	/* Initialize the IT87 chip */
	it87_init_device(pdev);

	/* Register sysfs hooks */
	err = sysfs_create_group(&dev->kobj, &it87_group);
	if (err)
		goto ERROR2;

	if (sio_data->beep_pin) {
		err = sysfs_create_group(&dev->kobj, &it87_group_beep);
		if (err)
			goto ERROR4;
	}

	/* Do not create fan files for disabled fans */
	fan_group = it87_get_fan_group(data);
	fan_beep_need_rw = 1;
	for (i = 0; i < 5; i++) {
		if (!(data->has_fan & (1 << i)))
			continue;
		err = sysfs_create_group(&dev->kobj, &fan_group[i]);
		if (err)
			goto ERROR4;

		if (sio_data->beep_pin) {
			err = sysfs_create_file(&dev->kobj,
						it87_attributes_fan_beep[i]);
			if (err)
				goto ERROR4;
			if (!fan_beep_need_rw)
				continue;

			/* As we have a single beep enable bit for all fans,
			 * only the first enabled fan has a writable attribute
			 * for it. */
			if (sysfs_chmod_file(&dev->kobj,
					     it87_attributes_fan_beep[i],
					     S_IRUGO | S_IWUSR))
				dev_dbg(dev, "chmod +w fan%d_beep failed\n",
					i + 1);
			fan_beep_need_rw = 0;
		}
	}

	if (enable_pwm_interface) {
		for (i = 0; i < 3; i++) {
			if (sio_data->skip_pwm & (1 << i))
				continue;
			err = sysfs_create_group(&dev->kobj,
						 &it87_group_pwm[i]);
			if (err)
				goto ERROR4;

			if (!has_old_autopwm(data))
				continue;
			err = sysfs_create_group(&dev->kobj,
						 &it87_group_autopwm[i]);
			if (err)
				goto ERROR4;
		}
	}

	if (!sio_data->skip_vid) {
		data->vrm = vid_which_vrm();
		/* VID reading from Super-I/O config space if available */
		data->vid = sio_data->vid_value;
		err = sysfs_create_group(&dev->kobj, &it87_group_vid);
		if (err)
			goto ERROR4;
	}

	/* Export labels for internal sensors */
	for (i = 0; i < 3; i++) {
		if (!(sio_data->internal & (1 << i)))
			continue;
		err = sysfs_create_file(&dev->kobj,
					it87_attributes_label[i]);
		if (err)
			goto ERROR4;
	}

	data->hwmon_dev = hwmon_device_register(dev);
	if (IS_ERR(data->hwmon_dev)) {
		err = PTR_ERR(data->hwmon_dev);
		goto ERROR4;
	}

	return 0;

ERROR4:
	it87_remove_files(dev);
ERROR2:
	platform_set_drvdata(pdev, NULL);
	kfree(data);
ERROR1:
	release_region(res->start, IT87_EC_EXTENT);
ERROR0:
	return err;
}

static int __devexit it87_remove(struct platform_device *pdev)
{
	struct it87_data *data = platform_get_drvdata(pdev);

	hwmon_device_unregister(data->hwmon_dev);
	it87_remove_files(&pdev->dev);

	release_region(data->addr, IT87_EC_EXTENT);
	platform_set_drvdata(pdev, NULL);
	kfree(data);

	return 0;
}

/* Must be called with data->update_lock held, except during initialization.
   We ignore the IT87 BUSY flag at this moment - it could lead to deadlocks,
   would slow down the IT87 access and should not be necessary. */
static int it87_read_value(struct it87_data *data, u8 reg)
{
	outb_p(reg, data->addr + IT87_ADDR_REG_OFFSET);
	return inb_p(data->addr + IT87_DATA_REG_OFFSET);
}

/* Must be called with data->update_lock held, except during initialization.
   We ignore the IT87 BUSY flag at this moment - it could lead to deadlocks,
   would slow down the IT87 access and should not be necessary. */
static void it87_write_value(struct it87_data *data, u8 reg, u8 value)
{
	outb_p(reg, data->addr + IT87_ADDR_REG_OFFSET);
	outb_p(value, data->addr + IT87_DATA_REG_OFFSET);
}

/* Return 1 if and only if the PWM interface is safe to use */
static int __devinit it87_check_pwm(struct device *dev)
{
	struct it87_data *data = dev_get_drvdata(dev);
	/* Some BIOSes fail to correctly configure the IT87 fans. All fans off
	 * and polarity set to active low is sign that this is the case so we
	 * disable pwm control to protect the user. */
	int tmp = it87_read_value(data, IT87_REG_FAN_CTL);
	if ((tmp & 0x87) == 0) {
		if (fix_pwm_polarity) {
			/* The user asks us to attempt a chip reconfiguration.
			 * This means switching to active high polarity and
			 * inverting all fan speed values. */
			int i;
			u8 pwm[3];

			for (i = 0; i < 3; i++)
				pwm[i] = it87_read_value(data,
							 IT87_REG_PWM(i));

			/* If any fan is in automatic pwm mode, the polarity
			 * might be correct, as suspicious as it seems, so we
			 * better don't change anything (but still disable the
			 * PWM interface). */
			if (!((pwm[0] | pwm[1] | pwm[2]) & 0x80)) {
				dev_info(dev, "Reconfiguring PWM to "
					 "active high polarity\n");
				it87_write_value(data, IT87_REG_FAN_CTL,
						 tmp | 0x87);
				for (i = 0; i < 3; i++)
					it87_write_value(data,
							 IT87_REG_PWM(i),
							 0x7f & ~pwm[i]);
				return 1;
			}

			dev_info(dev, "PWM configuration is "
				 "too broken to be fixed\n");
		}

		dev_info(dev, "Detected broken BIOS "
			 "defaults, disabling PWM interface\n");
		return 0;
	} else if (fix_pwm_polarity) {
		dev_info(dev, "PWM configuration looks "
			 "sane, won't touch\n");
	}

	return 1;
}

/* Called when we have found a new IT87. */
static void __devinit it87_init_device(struct platform_device *pdev)
{
	struct it87_sio_data *sio_data = pdev->dev.platform_data;
	struct it87_data *data = platform_get_drvdata(pdev);
	int tmp, i;
	u8 mask;

	/* For each PWM channel:
	 * - If it is in automatic mode, setting to manual mode should set
	 *   the fan to full speed by default.
	 * - If it is in manual mode, we need a mapping to temperature
	 *   channels to use when later setting to automatic mode later.
	 *   Use a 1:1 mapping by default (we are clueless.)
	 * In both cases, the value can (and should) be changed by the user
	 * prior to switching to a different mode.
	 * Note that this is no longer needed for the IT8721F and later, as
	 * these have separate registers for the temperature mapping and the
	 * manual duty cycle. */
	for (i = 0; i < 3; i++) {
		data->pwm_temp_map[i] = i;
		data->pwm_duty[i] = 0x7f;	/* Full speed */
		data->auto_pwm[i][3] = 0x7f;	/* Full speed, hard-coded */
	}

	/* Some chips seem to have default value 0xff for all limit
	 * registers. For low voltage limits it makes no sense and triggers
	 * alarms, so change to 0 instead. For high temperature limits, it
	 * means -1 degree C, which surprisingly doesn't trigger an alarm,
	 * but is still confusing, so change to 127 degrees C. */
	for (i = 0; i < 8; i++) {
		tmp = it87_read_value(data, IT87_REG_VIN_MIN(i));
		if (tmp == 0xff)
			it87_write_value(data, IT87_REG_VIN_MIN(i), 0);
	}
	for (i = 0; i < 3; i++) {
		tmp = it87_read_value(data, IT87_REG_TEMP_HIGH(i));
		if (tmp == 0xff)
			it87_write_value(data, IT87_REG_TEMP_HIGH(i), 127);
	}

	/* Temperature channels are not forcibly enabled, as they can be
	 * set to two different sensor types and we can't guess which one
	 * is correct for a given system. These channels can be enabled at
	 * run-time through the temp{1-3}_type sysfs accessors if needed. */

	/* Check if voltage monitors are reset manually or by some reason */
	tmp = it87_read_value(data, IT87_REG_VIN_ENABLE);
	if ((tmp & 0xff) == 0) {
		/* Enable all voltage monitors */
		it87_write_value(data, IT87_REG_VIN_ENABLE, 0xff);
	}

	/* Check if tachometers are reset manually or by some reason */
	mask = 0x70 & ~(sio_data->skip_fan << 4);
	data->fan_main_ctrl = it87_read_value(data, IT87_REG_FAN_MAIN_CTRL);
	if ((data->fan_main_ctrl & mask) == 0) {
		/* Enable all fan tachometers */
		data->fan_main_ctrl |= mask;
		it87_write_value(data, IT87_REG_FAN_MAIN_CTRL,
				 data->fan_main_ctrl);
	}
	data->has_fan = (data->fan_main_ctrl >> 4) & 0x07;

	/* Set tachometers to 16-bit mode if needed */
	if (has_16bit_fans(data)) {
		tmp = it87_read_value(data, IT87_REG_FAN_16BIT);
		if (~tmp & 0x07 & data->has_fan) {
			dev_dbg(&pdev->dev,
				"Setting fan1-3 to 16-bit mode\n");
			it87_write_value(data, IT87_REG_FAN_16BIT,
					 tmp | 0x07);
		}
		/* IT8705F only supports three fans. */
		if (data->type != it87) {
			if (tmp & (1 << 4))
				data->has_fan |= (1 << 3); /* fan4 enabled */
			if (tmp & (1 << 5))
				data->has_fan |= (1 << 4); /* fan5 enabled */
		}
	}

	/* Fan input pins may be used for alternative functions */
	data->has_fan &= ~sio_data->skip_fan;

	/* Start monitoring */
	it87_write_value(data, IT87_REG_CONFIG,
			 (it87_read_value(data, IT87_REG_CONFIG) & 0x3e)
			 | (update_vbat ? 0x41 : 0x01));
}

static void it87_update_pwm_ctrl(struct it87_data *data, int nr)
{
	data->pwm_ctrl[nr] = it87_read_value(data, IT87_REG_PWM(nr));
	if (data->type == it8721) {
		data->pwm_temp_map[nr] = data->pwm_ctrl[nr] & 0x03;
		data->pwm_duty[nr] = it87_read_value(data,
						     IT87_REG_PWM_DUTY(nr));
	} else {
		if (data->pwm_ctrl[nr] & 0x80)	/* Automatic mode */
			data->pwm_temp_map[nr] = data->pwm_ctrl[nr] & 0x03;
		else				/* Manual mode */
			data->pwm_duty[nr] = data->pwm_ctrl[nr] & 0x7f;
	}

	if (has_old_autopwm(data)) {
		int i;

		for (i = 0; i < 5 ; i++)
			data->auto_temp[nr][i] = it87_read_value(data,
						IT87_REG_AUTO_TEMP(nr, i));
		for (i = 0; i < 3 ; i++)
			data->auto_pwm[nr][i] = it87_read_value(data,
						IT87_REG_AUTO_PWM(nr, i));
	}
}

static struct it87_data *it87_update_device(struct device *dev)
{
	struct it87_data *data = dev_get_drvdata(dev);
	int i;

	mutex_lock(&data->update_lock);

	if (time_after(jiffies, data->last_updated + HZ + HZ / 2)
	    || !data->valid) {
		if (update_vbat) {
			/* Cleared after each update, so reenable.  Value
			   returned by this read will be previous value */
			it87_write_value(data, IT87_REG_CONFIG,
				it87_read_value(data, IT87_REG_CONFIG) | 0x40);
		}
		for (i = 0; i <= 7; i++) {
			data->in[i] =
				it87_read_value(data, IT87_REG_VIN(i));
			data->in_min[i] =
				it87_read_value(data, IT87_REG_VIN_MIN(i));
			data->in_max[i] =
				it87_read_value(data, IT87_REG_VIN_MAX(i));
		}
		/* in8 (battery) has no limit registers */
		data->in[8] = it87_read_value(data, IT87_REG_VIN(8));

		for (i = 0; i < 5; i++) {
			/* Skip disabled fans */
			if (!(data->has_fan & (1 << i)))
				continue;

			data->fan_min[i] =
				it87_read_value(data, IT87_REG_FAN_MIN[i]);
			data->fan[i] = it87_read_value(data,
				       IT87_REG_FAN[i]);
			/* Add high byte if in 16-bit mode */
			if (has_16bit_fans(data)) {
				data->fan[i] |= it87_read_value(data,
						IT87_REG_FANX[i]) << 8;
				data->fan_min[i] |= it87_read_value(data,
						IT87_REG_FANX_MIN[i]) << 8;
			}
		}
		for (i = 0; i < 3; i++) {
			data->temp[i] =
				it87_read_value(data, IT87_REG_TEMP(i));
			data->temp_high[i] =
				it87_read_value(data, IT87_REG_TEMP_HIGH(i));
			data->temp_low[i] =
				it87_read_value(data, IT87_REG_TEMP_LOW(i));
		}

		/* Newer chips don't have clock dividers */
		if ((data->has_fan & 0x07) && !has_16bit_fans(data)) {
			i = it87_read_value(data, IT87_REG_FAN_DIV);
			data->fan_div[0] = i & 0x07;
			data->fan_div[1] = (i >> 3) & 0x07;
			data->fan_div[2] = (i & 0x40) ? 3 : 1;
		}

		data->alarms =
			it87_read_value(data, IT87_REG_ALARM1) |
			(it87_read_value(data, IT87_REG_ALARM2) << 8) |
			(it87_read_value(data, IT87_REG_ALARM3) << 16);
		data->beeps = it87_read_value(data, IT87_REG_BEEP_ENABLE);

		data->fan_main_ctrl = it87_read_value(data,
				IT87_REG_FAN_MAIN_CTRL);
		data->fan_ctl = it87_read_value(data, IT87_REG_FAN_CTL);
		for (i = 0; i < 3; i++)
			it87_update_pwm_ctrl(data, i);

		data->sensor = it87_read_value(data, IT87_REG_TEMP_ENABLE);
		/* The 8705 does not have VID capability.
		   The 8718 and later don't use IT87_REG_VID for the
		   same purpose. */
		if (data->type == it8712 || data->type == it8716) {
			data->vid = it87_read_value(data, IT87_REG_VID);
			/* The older IT8712F revisions had only 5 VID pins,
			   but we assume it is always safe to read 6 bits. */
			data->vid &= 0x3f;
		}
		data->last_updated = jiffies;
		data->valid = 1;
	}

	mutex_unlock(&data->update_lock);

	return data;
}

static int __init it87_device_add(unsigned short address,
				  const struct it87_sio_data *sio_data)
{
	struct resource res = {
		.start	= address + IT87_EC_OFFSET,
		.end	= address + IT87_EC_OFFSET + IT87_EC_EXTENT - 1,
		.name	= DRVNAME,
		.flags	= IORESOURCE_IO,
	};
	int err;

	err = acpi_check_resource_conflict(&res);
	if (err)
		goto exit;

	pdev = platform_device_alloc(DRVNAME, address);
	if (!pdev) {
		err = -ENOMEM;
		pr_err("Device allocation failed\n");
		goto exit;
	}

	err = platform_device_add_resources(pdev, &res, 1);
	if (err) {
		pr_err("Device resource addition failed (%d)\n", err);
		goto exit_device_put;
	}

	err = platform_device_add_data(pdev, sio_data,
				       sizeof(struct it87_sio_data));
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

static int __init sm_it87_init(void)
{
	int err;
	unsigned short isa_address = 0;
	struct it87_sio_data sio_data;

	memset(&sio_data, 0, sizeof(struct it87_sio_data));
	err = it87_find(&isa_address, &sio_data);
	if (err)
		return err;
	err = platform_driver_register(&it87_driver);
	if (err)
		return err;

	err = it87_device_add(isa_address, &sio_data);
	if (err) {
		platform_driver_unregister(&it87_driver);
		return err;
	}

	return 0;
}

static void __exit sm_it87_exit(void)
{
	platform_device_unregister(pdev);
	platform_driver_unregister(&it87_driver);
}


MODULE_AUTHOR("Chris Gauthron, "
	      "Jean Delvare <khali@linux-fr.org>");
MODULE_DESCRIPTION("IT8705F/IT871xF/IT872xF hardware monitoring driver");
module_param(update_vbat, bool, 0);
MODULE_PARM_DESC(update_vbat, "Update vbat if set else return powerup value");
module_param(fix_pwm_polarity, bool, 0);
MODULE_PARM_DESC(fix_pwm_polarity,
		 "Force PWM polarity to active high (DANGEROUS)");
MODULE_LICENSE("GPL");

module_init(sm_it87_init);
module_exit(sm_it87_exit);
