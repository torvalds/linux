/***************************************************************************
 *   Copyright (C) 2006 by Hans Edgington <hans@edgington.nl>              *
 *   Copyright (C) 2007 by Hans de Goede  <j.w.r.degoede@hhs.nl>           *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************/

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/jiffies.h>
#include <linux/platform_device.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/err.h>
#include <linux/mutex.h>
#include <asm/io.h>

#define DRVNAME "f71882fg"

#define SIO_F71882FG_LD_HWM	0x04	/* Hardware monitor logical device*/
#define SIO_UNLOCK_KEY		0x87	/* Key to enable Super-I/O */
#define SIO_LOCK_KEY		0xAA	/* Key to diasble Super-I/O */

#define SIO_REG_LDSEL		0x07	/* Logical device select */
#define SIO_REG_DEVID		0x20	/* Device ID (2 bytes) */
#define SIO_REG_DEVREV		0x22	/* Device revision */
#define SIO_REG_MANID		0x23	/* Fintek ID (2 bytes) */
#define SIO_REG_ENABLE		0x30	/* Logical device enable */
#define SIO_REG_ADDR		0x60	/* Logical device address (2 bytes) */

#define SIO_FINTEK_ID		0x1934	/* Manufacturers ID */
#define SIO_F71882_ID		0x0541	/* Chipset ID */

#define REGION_LENGTH		8
#define ADDR_REG_OFFSET		5
#define DATA_REG_OFFSET		6

#define F71882FG_REG_PECI		0x0A

#define F71882FG_REG_IN_STATUS		0x12
#define F71882FG_REG_IN_BEEP		0x13
#define F71882FG_REG_IN(nr)		(0x20  + (nr))
#define F71882FG_REG_IN1_HIGH		0x32

#define F71882FG_REG_FAN(nr)		(0xA0 + (16 * (nr)))
#define F71882FG_REG_FAN_STATUS		0x92
#define F71882FG_REG_FAN_BEEP		0x93

#define F71882FG_REG_TEMP(nr)		(0x72 + 2 * (nr))
#define F71882FG_REG_TEMP_OVT(nr)	(0x82 + 2 * (nr))
#define F71882FG_REG_TEMP_HIGH(nr)	(0x83 + 2 * (nr))
#define F71882FG_REG_TEMP_STATUS	0x62
#define F71882FG_REG_TEMP_BEEP		0x63
#define F71882FG_REG_TEMP_HYST1		0x6C
#define F71882FG_REG_TEMP_HYST23	0x6D
#define F71882FG_REG_TEMP_TYPE		0x6B
#define F71882FG_REG_TEMP_DIODE_OPEN	0x6F

#define	F71882FG_REG_START		0x01

#define FAN_MIN_DETECT			366 /* Lowest detectable fanspeed */

static struct platform_device *f71882fg_pdev = NULL;

/* Super-I/O Function prototypes */
static inline int superio_inb(int base, int reg);
static inline int superio_inw(int base, int reg);
static inline void superio_enter(int base);
static inline void superio_select(int base, int ld);
static inline void superio_exit(int base);

static inline u16 fan_from_reg ( u16 reg );

struct f71882fg_data {
	unsigned short addr;
	struct device *hwmon_dev;

	struct mutex update_lock;
	char valid;			/* !=0 if following fields are valid */
	unsigned long last_updated;	/* In jiffies */
	unsigned long last_limits;	/* In jiffies */

	/* Register Values */
	u8	in[9];
	u8	in1_max;
	u8	in_status;
	u8	in_beep;
	u16	fan[4];
	u8	fan_status;
	u8	fan_beep;
	u8	temp[3];
	u8	temp_ovt[3];
	u8	temp_high[3];
	u8	temp_hyst[3];
	u8	temp_type[3];
	u8	temp_status;
	u8	temp_beep;
	u8	temp_diode_open;
};

static u8 f71882fg_read8(struct f71882fg_data *data, u8 reg);
static u16 f71882fg_read16(struct f71882fg_data *data, u8 reg);
static void f71882fg_write8(struct f71882fg_data *data, u8 reg, u8 val);

/* Sysfs in*/
static ssize_t show_in(struct device *dev, struct device_attribute *devattr,
	char *buf);
static ssize_t show_in_max(struct device *dev, struct device_attribute
	*devattr, char *buf);
static ssize_t store_in_max(struct device *dev, struct device_attribute
	*devattr, const char *buf, size_t count);
static ssize_t show_in_beep(struct device *dev, struct device_attribute
	*devattr, char *buf);
static ssize_t store_in_beep(struct device *dev, struct device_attribute
	*devattr, const char *buf, size_t count);
static ssize_t show_in_alarm(struct device *dev, struct device_attribute
	*devattr, char *buf);
/* Sysfs Fan */
static ssize_t show_fan(struct device *dev, struct device_attribute *devattr,
	char *buf);
static ssize_t show_fan_beep(struct device *dev, struct device_attribute
	*devattr, char *buf);
static ssize_t store_fan_beep(struct device *dev, struct device_attribute
	*devattr, const char *buf, size_t count);
static ssize_t show_fan_alarm(struct device *dev, struct device_attribute
	*devattr, char *buf);
/* Sysfs Temp */
static ssize_t show_temp(struct device *dev, struct device_attribute
	*devattr, char *buf);
static ssize_t show_temp_max(struct device *dev, struct device_attribute
	*devattr, char *buf);
static ssize_t store_temp_max(struct device *dev, struct device_attribute
	*devattr, const char *buf, size_t count);
static ssize_t show_temp_max_hyst(struct device *dev, struct device_attribute
	*devattr, char *buf);
static ssize_t store_temp_max_hyst(struct device *dev, struct device_attribute
	*devattr, const char *buf, size_t count);
static ssize_t show_temp_crit(struct device *dev, struct device_attribute
	*devattr, char *buf);
static ssize_t store_temp_crit(struct device *dev, struct device_attribute
	*devattr, const char *buf, size_t count);
static ssize_t show_temp_crit_hyst(struct device *dev, struct device_attribute
	*devattr, char *buf);
static ssize_t show_temp_type(struct device *dev, struct device_attribute
	*devattr, char *buf);
static ssize_t show_temp_beep(struct device *dev, struct device_attribute
	*devattr, char *buf);
static ssize_t store_temp_beep(struct device *dev, struct device_attribute
	*devattr, const char *buf, size_t count);
static ssize_t show_temp_alarm(struct device *dev, struct device_attribute
	*devattr, char *buf);
static ssize_t show_temp_fault(struct device *dev, struct device_attribute
	*devattr, char *buf);
/* Sysfs misc */
static ssize_t show_name(struct device *dev, struct device_attribute *devattr,
	char *buf);

static int __devinit f71882fg_probe(struct platform_device * pdev);
static int __devexit f71882fg_remove(struct platform_device *pdev);
static int __init f71882fg_init(void);
static int __init f71882fg_find(int sioaddr, unsigned short *address);
static int __init f71882fg_device_add(unsigned short address);
static void __exit f71882fg_exit(void);

static struct platform_driver f71882fg_driver = {
	.driver = {
		.owner	= THIS_MODULE,
		.name	= DRVNAME,
	},
	.probe		= f71882fg_probe,
	.remove		= __devexit_p(f71882fg_remove),
};

static struct device_attribute f71882fg_dev_attr[] =
{
	__ATTR( name, S_IRUGO, show_name, NULL ),
};

static struct sensor_device_attribute f71882fg_in_temp_attr[] =
{
	SENSOR_ATTR(in0_input, S_IRUGO, show_in, NULL, 0),
	SENSOR_ATTR(in1_input, S_IRUGO, show_in, NULL, 1),
	SENSOR_ATTR(in1_max, S_IRUGO|S_IWUSR, show_in_max, store_in_max, 1),
	SENSOR_ATTR(in1_beep, S_IRUGO|S_IWUSR, show_in_beep, store_in_beep, 1),
	SENSOR_ATTR(in1_alarm, S_IRUGO, show_in_alarm, NULL, 1),
	SENSOR_ATTR(in2_input, S_IRUGO, show_in, NULL, 2),
	SENSOR_ATTR(in3_input, S_IRUGO, show_in, NULL, 3),
	SENSOR_ATTR(in4_input, S_IRUGO, show_in, NULL, 4),
	SENSOR_ATTR(in5_input, S_IRUGO, show_in, NULL, 5),
	SENSOR_ATTR(in6_input, S_IRUGO, show_in, NULL, 6),
	SENSOR_ATTR(in7_input, S_IRUGO, show_in, NULL, 7),
	SENSOR_ATTR(in8_input, S_IRUGO, show_in, NULL, 8),
	SENSOR_ATTR(temp1_input, S_IRUGO, show_temp, NULL, 0),
	SENSOR_ATTR(temp1_max, S_IRUGO|S_IWUSR, show_temp_max,
		store_temp_max, 0),
	SENSOR_ATTR(temp1_max_hyst, S_IRUGO|S_IWUSR, show_temp_max_hyst,
		store_temp_max_hyst, 0),
	SENSOR_ATTR(temp1_crit, S_IRUGO|S_IWUSR, show_temp_crit,
		store_temp_crit, 0),
	SENSOR_ATTR(temp1_crit_hyst, S_IRUGO, show_temp_crit_hyst, NULL, 0),
	SENSOR_ATTR(temp1_type, S_IRUGO, show_temp_type, NULL, 0),
	SENSOR_ATTR(temp1_beep, S_IRUGO|S_IWUSR, show_temp_beep,
		store_temp_beep, 0),
	SENSOR_ATTR(temp1_alarm, S_IRUGO, show_temp_alarm, NULL, 0),
	SENSOR_ATTR(temp1_fault, S_IRUGO, show_temp_fault, NULL, 0),
	SENSOR_ATTR(temp2_input, S_IRUGO, show_temp, NULL, 1),
	SENSOR_ATTR(temp2_max, S_IRUGO|S_IWUSR, show_temp_max,
		store_temp_max, 1),
	SENSOR_ATTR(temp2_max_hyst, S_IRUGO|S_IWUSR, show_temp_max_hyst,
		store_temp_max_hyst, 1),
	SENSOR_ATTR(temp2_crit, S_IRUGO|S_IWUSR, show_temp_crit,
		store_temp_crit, 1),
	SENSOR_ATTR(temp2_crit_hyst, S_IRUGO, show_temp_crit_hyst, NULL, 1),
	SENSOR_ATTR(temp2_type, S_IRUGO, show_temp_type, NULL, 1),
	SENSOR_ATTR(temp2_beep, S_IRUGO|S_IWUSR, show_temp_beep,
		store_temp_beep, 1),
	SENSOR_ATTR(temp2_alarm, S_IRUGO, show_temp_alarm, NULL, 1),
	SENSOR_ATTR(temp2_fault, S_IRUGO, show_temp_fault, NULL, 1),
	SENSOR_ATTR(temp3_input, S_IRUGO, show_temp, NULL, 2),
	SENSOR_ATTR(temp3_max, S_IRUGO|S_IWUSR, show_temp_max,
		store_temp_max, 2),
	SENSOR_ATTR(temp3_max_hyst, S_IRUGO|S_IWUSR, show_temp_max_hyst,
		store_temp_max_hyst, 2),
	SENSOR_ATTR(temp3_crit, S_IRUGO|S_IWUSR, show_temp_crit,
		store_temp_crit, 2),
	SENSOR_ATTR(temp3_crit_hyst, S_IRUGO, show_temp_crit_hyst, NULL, 2),
	SENSOR_ATTR(temp3_type, S_IRUGO, show_temp_type, NULL, 2),
	SENSOR_ATTR(temp3_beep, S_IRUGO|S_IWUSR, show_temp_beep,
		store_temp_beep, 2),
	SENSOR_ATTR(temp3_alarm, S_IRUGO, show_temp_alarm, NULL, 2),
	SENSOR_ATTR(temp3_fault, S_IRUGO, show_temp_fault, NULL, 2)
};

static struct sensor_device_attribute f71882fg_fan_attr[] =
{
	SENSOR_ATTR(fan1_input, S_IRUGO, show_fan, NULL, 0),
	SENSOR_ATTR(fan1_beep, S_IRUGO|S_IWUSR, show_fan_beep,
		store_fan_beep, 0),
	SENSOR_ATTR(fan1_alarm, S_IRUGO, show_fan_alarm, NULL, 0),
	SENSOR_ATTR(fan2_input, S_IRUGO, show_fan, NULL, 1),
	SENSOR_ATTR(fan2_beep, S_IRUGO|S_IWUSR, show_fan_beep,
		store_fan_beep, 1),
	SENSOR_ATTR(fan2_alarm, S_IRUGO, show_fan_alarm, NULL, 1),
	SENSOR_ATTR(fan3_input, S_IRUGO, show_fan, NULL, 2),
	SENSOR_ATTR(fan3_beep, S_IRUGO|S_IWUSR, show_fan_beep,
		store_fan_beep, 2),
	SENSOR_ATTR(fan3_alarm, S_IRUGO, show_fan_alarm, NULL, 2),
	SENSOR_ATTR(fan4_input, S_IRUGO, show_fan, NULL, 3),
	SENSOR_ATTR(fan4_beep, S_IRUGO|S_IWUSR, show_fan_beep,
		store_fan_beep, 3),
	SENSOR_ATTR(fan4_alarm, S_IRUGO, show_fan_alarm, NULL, 3)
};


/* Super I/O functions */
static inline int superio_inb(int base, int reg)
{
	outb(reg, base);
	return inb(base + 1);
}

static int superio_inw(int base, int reg)
{
	int val;
	outb(reg++, base);
	val = inb(base + 1) << 8;
	outb(reg, base);
	val |= inb(base + 1);
	return val;
}

static inline void superio_enter(int base)
{
	/* according to the datasheet the key must be send twice! */
	outb( SIO_UNLOCK_KEY, base);
	outb( SIO_UNLOCK_KEY, base);
}

static inline void superio_select( int base, int ld)
{
	outb(SIO_REG_LDSEL, base);
	outb(ld, base + 1);
}

static inline void superio_exit(int base)
{
	outb(SIO_LOCK_KEY, base);
}

static inline u16 fan_from_reg(u16 reg)
{
	return reg ? (1500000 / reg) : 0;
}

static u8 f71882fg_read8(struct f71882fg_data *data, u8 reg)
{
	u8 val;

	outb(reg, data->addr + ADDR_REG_OFFSET);
	val = inb(data->addr + DATA_REG_OFFSET);

	return val;
}

static u16 f71882fg_read16(struct f71882fg_data *data, u8 reg)
{
	u16 val;

	outb(reg++, data->addr + ADDR_REG_OFFSET);
	val = inb(data->addr + DATA_REG_OFFSET) << 8;
	outb(reg, data->addr + ADDR_REG_OFFSET);
	val |= inb(data->addr + DATA_REG_OFFSET);

	return val;
}

static void f71882fg_write8(struct f71882fg_data *data, u8 reg, u8 val)
{
	outb(reg, data->addr + ADDR_REG_OFFSET);
	outb(val, data->addr + DATA_REG_OFFSET);
}

static struct f71882fg_data *f71882fg_update_device(struct device * dev)
{
	struct f71882fg_data *data = dev_get_drvdata(dev);
	int nr, reg, reg2;

	mutex_lock(&data->update_lock);

	/* Update once every 60 seconds */
	if ( time_after(jiffies, data->last_limits + 60 * HZ ) ||
			!data->valid) {
		data->in1_max = f71882fg_read8(data, F71882FG_REG_IN1_HIGH);
		data->in_beep = f71882fg_read8(data, F71882FG_REG_IN_BEEP);

		/* Get High & boundary temps*/
		for (nr = 0; nr < 3; nr++) {
			data->temp_ovt[nr] = f71882fg_read8(data,
						F71882FG_REG_TEMP_OVT(nr));
			data->temp_high[nr] = f71882fg_read8(data,
						F71882FG_REG_TEMP_HIGH(nr));
		}

		/* Have to hardcode hyst*/
		data->temp_hyst[0] = f71882fg_read8(data,
						F71882FG_REG_TEMP_HYST1) >> 4;
		/* Hyst temps 2 & 3 stored in same register */
		reg = f71882fg_read8(data, F71882FG_REG_TEMP_HYST23);
		data->temp_hyst[1] = reg & 0x0F;
		data->temp_hyst[2] = reg >> 4;

		/* Have to hardcode type, because temp1 is special */
		reg  = f71882fg_read8(data, F71882FG_REG_TEMP_TYPE);
		reg2 = f71882fg_read8(data, F71882FG_REG_PECI);
		if ((reg2 & 0x03) == 0x01)
			data->temp_type[0] = 6 /* PECI */;
		else if ((reg2 & 0x03) == 0x02)
			data->temp_type[0] = 5 /* AMDSI */;
		else
			data->temp_type[0] = (reg & 0x02) ? 2 : 4;

		data->temp_type[1] = (reg & 0x04) ? 2 : 4;
		data->temp_type[2] = (reg & 0x08) ? 2 : 4;

		data->temp_beep = f71882fg_read8(data, F71882FG_REG_TEMP_BEEP);

		data->fan_beep = f71882fg_read8(data, F71882FG_REG_FAN_BEEP);

		data->last_limits = jiffies;
	}

	/* Update every second */
	if (time_after(jiffies, data->last_updated + HZ) || !data->valid) {
		data->temp_status = f71882fg_read8(data,
						F71882FG_REG_TEMP_STATUS);
		data->temp_diode_open = f71882fg_read8(data,
						F71882FG_REG_TEMP_DIODE_OPEN);
		for (nr = 0; nr < 3; nr++)
			data->temp[nr] = f71882fg_read8(data,
						F71882FG_REG_TEMP(nr));

		data->fan_status = f71882fg_read8(data,
						F71882FG_REG_FAN_STATUS);
		for (nr = 0; nr < 4; nr++)
			data->fan[nr] = f71882fg_read16(data,
						F71882FG_REG_FAN(nr));

		data->in_status = f71882fg_read8(data,
						F71882FG_REG_IN_STATUS);
		for (nr = 0; nr < 9; nr++)
			data->in[nr] = f71882fg_read8(data,
						F71882FG_REG_IN(nr));

		data->last_updated = jiffies;
		data->valid = 1;
	}

	mutex_unlock(&data->update_lock);

	return data;
}

/* Sysfs Interface */
static ssize_t show_fan(struct device *dev, struct device_attribute *devattr,
	char *buf)
{
	struct f71882fg_data *data = f71882fg_update_device(dev);
	int nr = to_sensor_dev_attr(devattr)->index;
	int speed = fan_from_reg(data->fan[nr]);

	if (speed == FAN_MIN_DETECT)
		speed = 0;

	return sprintf(buf, "%d\n", speed);
}

static ssize_t show_fan_beep(struct device *dev, struct device_attribute
	*devattr, char *buf)
{
	struct f71882fg_data *data = f71882fg_update_device(dev);
	int nr = to_sensor_dev_attr(devattr)->index;

	if (data->fan_beep & (1 << nr))
		return sprintf(buf, "1\n");
	else
		return sprintf(buf, "0\n");
}

static ssize_t store_fan_beep(struct device *dev, struct device_attribute
	*devattr, const char *buf, size_t count)
{
	struct f71882fg_data *data = dev_get_drvdata(dev);
	int nr = to_sensor_dev_attr(devattr)->index;
	int val = simple_strtoul(buf, NULL, 10);

	mutex_lock(&data->update_lock);
	if (val)
		data->fan_beep |= 1 << nr;
	else
		data->fan_beep &= ~(1 << nr);

	f71882fg_write8(data, F71882FG_REG_FAN_BEEP, data->fan_beep);
	mutex_unlock(&data->update_lock);

	return count;
}

static ssize_t show_fan_alarm(struct device *dev, struct device_attribute
	*devattr, char *buf)
{
	struct f71882fg_data *data = f71882fg_update_device(dev);
	int nr = to_sensor_dev_attr(devattr)->index;

	if (data->fan_status & (1 << nr))
		return sprintf(buf, "1\n");
	else
		return sprintf(buf, "0\n");
}

static ssize_t show_in(struct device *dev, struct device_attribute *devattr,
	char *buf)
{
	struct f71882fg_data *data = f71882fg_update_device(dev);
	int nr = to_sensor_dev_attr(devattr)->index;

	return sprintf(buf, "%d\n", data->in[nr] * 8);
}

static ssize_t show_in_max(struct device *dev, struct device_attribute
	*devattr, char *buf)
{
	struct f71882fg_data *data = f71882fg_update_device(dev);

	return sprintf(buf, "%d\n", data->in1_max * 8);
}

static ssize_t store_in_max(struct device *dev, struct device_attribute
	*devattr, const char *buf, size_t count)
{
	struct f71882fg_data *data = dev_get_drvdata(dev);
	int val = simple_strtoul(buf, NULL, 10) / 8;

	if (val > 255)
		val = 255;

	mutex_lock(&data->update_lock);
	f71882fg_write8(data, F71882FG_REG_IN1_HIGH, val);
	data->in1_max = val;
	mutex_unlock(&data->update_lock);

	return count;
}

static ssize_t show_in_beep(struct device *dev, struct device_attribute
	*devattr, char *buf)
{
	struct f71882fg_data *data = f71882fg_update_device(dev);
	int nr = to_sensor_dev_attr(devattr)->index;

	if (data->in_beep & (1 << nr))
		return sprintf(buf, "1\n");
	else
		return sprintf(buf, "0\n");
}

static ssize_t store_in_beep(struct device *dev, struct device_attribute
	*devattr, const char *buf, size_t count)
{
	struct f71882fg_data *data = dev_get_drvdata(dev);
	int nr = to_sensor_dev_attr(devattr)->index;
	int val = simple_strtoul(buf, NULL, 10);

	mutex_lock(&data->update_lock);
	if (val)
		data->in_beep |= 1 << nr;
	else
		data->in_beep &= ~(1 << nr);

	f71882fg_write8(data, F71882FG_REG_IN_BEEP, data->in_beep);
	mutex_unlock(&data->update_lock);

	return count;
}

static ssize_t show_in_alarm(struct device *dev, struct device_attribute
	*devattr, char *buf)
{
	struct f71882fg_data *data = f71882fg_update_device(dev);
	int nr = to_sensor_dev_attr(devattr)->index;

	if (data->in_status & (1 << nr))
		return sprintf(buf, "1\n");
	else
		return sprintf(buf, "0\n");
}

static ssize_t show_temp(struct device *dev, struct device_attribute *devattr,
	char *buf)
{
	struct f71882fg_data *data = f71882fg_update_device(dev);
	int nr = to_sensor_dev_attr(devattr)->index;

	return sprintf(buf, "%d\n", data->temp[nr] * 1000);
}

static ssize_t show_temp_max(struct device *dev, struct device_attribute
	*devattr, char *buf)
{
	struct f71882fg_data *data = f71882fg_update_device(dev);
	int nr = to_sensor_dev_attr(devattr)->index;

	return sprintf(buf, "%d\n", data->temp_high[nr] * 1000);
}

static ssize_t store_temp_max(struct device *dev, struct device_attribute
	*devattr, const char *buf, size_t count)
{
	struct f71882fg_data *data = dev_get_drvdata(dev);
	int nr = to_sensor_dev_attr(devattr)->index;
	int val = simple_strtoul(buf, NULL, 10) / 1000;

	if (val > 255)
		val = 255;

	mutex_lock(&data->update_lock);
	f71882fg_write8(data, F71882FG_REG_TEMP_HIGH(nr), val);
	data->temp_high[nr] = val;
	mutex_unlock(&data->update_lock);

	return count;
}

static ssize_t show_temp_max_hyst(struct device *dev, struct device_attribute
	*devattr, char *buf)
{
	struct f71882fg_data *data = f71882fg_update_device(dev);
	int nr = to_sensor_dev_attr(devattr)->index;

	return sprintf(buf, "%d\n",
		(data->temp_high[nr] - data->temp_hyst[nr]) * 1000);
}

static ssize_t store_temp_max_hyst(struct device *dev, struct device_attribute
	*devattr, const char *buf, size_t count)
{
	struct f71882fg_data *data = dev_get_drvdata(dev);
	int nr = to_sensor_dev_attr(devattr)->index;
	int val = simple_strtoul(buf, NULL, 10) / 1000;
	ssize_t ret = count;

	mutex_lock(&data->update_lock);

	/* convert abs to relative and check */
	val = data->temp_high[nr] - val;
	if (val < 0 || val > 15) {
		ret = -EINVAL;
		goto store_temp_max_hyst_exit;
	}

	data->temp_hyst[nr] = val;

	/* convert value to register contents */
	switch (nr) {
		case 0:
			val = val << 4;
			break;
		case 1:
			val = val | (data->temp_hyst[2] << 4);
			break;
		case 2:
			val = data->temp_hyst[1] | (val << 4);
			break;
	}

	f71882fg_write8(data, nr ? F71882FG_REG_TEMP_HYST23 :
		F71882FG_REG_TEMP_HYST1, val);

store_temp_max_hyst_exit:
	mutex_unlock(&data->update_lock);
	return ret;
}

static ssize_t show_temp_crit(struct device *dev, struct device_attribute
	*devattr, char *buf)
{
	struct f71882fg_data *data = f71882fg_update_device(dev);
	int nr = to_sensor_dev_attr(devattr)->index;

	return sprintf(buf, "%d\n", data->temp_ovt[nr] * 1000);
}

static ssize_t store_temp_crit(struct device *dev, struct device_attribute
	*devattr, const char *buf, size_t count)
{
	struct f71882fg_data *data = dev_get_drvdata(dev);
	int nr = to_sensor_dev_attr(devattr)->index;
	int val = simple_strtoul(buf, NULL, 10) / 1000;

	if (val > 255)
		val = 255;

	mutex_lock(&data->update_lock);
	f71882fg_write8(data, F71882FG_REG_TEMP_OVT(nr), val);
	data->temp_ovt[nr] = val;
	mutex_unlock(&data->update_lock);

	return count;
}

static ssize_t show_temp_crit_hyst(struct device *dev, struct device_attribute
	*devattr, char *buf)
{
	struct f71882fg_data *data = f71882fg_update_device(dev);
	int nr = to_sensor_dev_attr(devattr)->index;

	return sprintf(buf, "%d\n",
		(data->temp_ovt[nr] - data->temp_hyst[nr]) * 1000);
}

static ssize_t show_temp_type(struct device *dev, struct device_attribute
	*devattr, char *buf)
{
	struct f71882fg_data *data = f71882fg_update_device(dev);
	int nr = to_sensor_dev_attr(devattr)->index;

	return sprintf(buf, "%d\n", data->temp_type[nr]);
}

static ssize_t show_temp_beep(struct device *dev, struct device_attribute
	*devattr, char *buf)
{
	struct f71882fg_data *data = f71882fg_update_device(dev);
	int nr = to_sensor_dev_attr(devattr)->index;

	if (data->temp_beep & (1 << (nr + 1)))
		return sprintf(buf, "1\n");
	else
		return sprintf(buf, "0\n");
}

static ssize_t store_temp_beep(struct device *dev, struct device_attribute
	*devattr, const char *buf, size_t count)
{
	struct f71882fg_data *data = dev_get_drvdata(dev);
	int nr = to_sensor_dev_attr(devattr)->index;
	int val = simple_strtoul(buf, NULL, 10);

	mutex_lock(&data->update_lock);
	if (val)
		data->temp_beep |= 1 << (nr + 1);
	else
		data->temp_beep &= ~(1 << (nr + 1));

	f71882fg_write8(data, F71882FG_REG_TEMP_BEEP, data->temp_beep);
	mutex_unlock(&data->update_lock);

	return count;
}

static ssize_t show_temp_alarm(struct device *dev, struct device_attribute
	*devattr, char *buf)
{
	struct f71882fg_data *data = f71882fg_update_device(dev);
	int nr = to_sensor_dev_attr(devattr)->index;

	if (data->temp_status & (1 << (nr + 1)))
		return sprintf(buf, "1\n");
	else
		return sprintf(buf, "0\n");
}

static ssize_t show_temp_fault(struct device *dev, struct device_attribute
	*devattr, char *buf)
{
	struct f71882fg_data *data = f71882fg_update_device(dev);
	int nr = to_sensor_dev_attr(devattr)->index;

	if (data->temp_diode_open & (1 << (nr + 1)))
		return sprintf(buf, "1\n");
	else
		return sprintf(buf, "0\n");
}

static ssize_t show_name(struct device *dev, struct device_attribute *devattr,
	char *buf)
{
	return sprintf(buf, DRVNAME "\n");
}


static int __devinit f71882fg_probe(struct platform_device * pdev)
{
	struct f71882fg_data *data;
	int err, i;
	u8 start_reg;

	if (!(data = kzalloc(sizeof(struct f71882fg_data), GFP_KERNEL)))
		return -ENOMEM;

	data->addr = platform_get_resource(pdev, IORESOURCE_IO, 0)->start;
	mutex_init(&data->update_lock);
	platform_set_drvdata(pdev, data);

	/* Register sysfs interface files */
	for (i = 0; i < ARRAY_SIZE(f71882fg_dev_attr); i++) {
		err = device_create_file(&pdev->dev, &f71882fg_dev_attr[i]);
		if (err)
			goto exit_unregister_sysfs;
	}

	start_reg = f71882fg_read8(data, F71882FG_REG_START);
	if (start_reg & 0x01) {
		for (i = 0; i < ARRAY_SIZE(f71882fg_in_temp_attr); i++) {
			err = device_create_file(&pdev->dev,
					&f71882fg_in_temp_attr[i].dev_attr);
			if (err)
				goto exit_unregister_sysfs;
		}
	}

	if (start_reg & 0x02) {
		for (i = 0; i < ARRAY_SIZE(f71882fg_fan_attr); i++) {
			err = device_create_file(&pdev->dev,
					&f71882fg_fan_attr[i].dev_attr);
			if (err)
				goto exit_unregister_sysfs;
		}
	}

	data->hwmon_dev = hwmon_device_register(&pdev->dev);
	if (IS_ERR(data->hwmon_dev)) {
		err = PTR_ERR(data->hwmon_dev);
		goto exit_unregister_sysfs;
	}

	return 0;

exit_unregister_sysfs:
	for (i = 0; i < ARRAY_SIZE(f71882fg_dev_attr); i++)
		device_remove_file(&pdev->dev, &f71882fg_dev_attr[i]);

	for (i = 0; i < ARRAY_SIZE(f71882fg_in_temp_attr); i++)
		device_remove_file(&pdev->dev,
					&f71882fg_in_temp_attr[i].dev_attr);

	for (i = 0; i < ARRAY_SIZE(f71882fg_fan_attr); i++)
		device_remove_file(&pdev->dev, &f71882fg_fan_attr[i].dev_attr);

	kfree(data);

	return err;
}

static int __devexit f71882fg_remove(struct platform_device *pdev)
{
	int i;
	struct f71882fg_data *data = platform_get_drvdata(pdev);

	platform_set_drvdata(pdev, NULL);
	hwmon_device_unregister(data->hwmon_dev);

	for (i = 0; i < ARRAY_SIZE(f71882fg_dev_attr); i++)
		device_remove_file(&pdev->dev, &f71882fg_dev_attr[i]);

	for (i = 0; i < ARRAY_SIZE(f71882fg_in_temp_attr); i++)
		device_remove_file(&pdev->dev,
					&f71882fg_in_temp_attr[i].dev_attr);

	for (i = 0; i < ARRAY_SIZE(f71882fg_fan_attr); i++)
		device_remove_file(&pdev->dev, &f71882fg_fan_attr[i].dev_attr);

	kfree(data);

	return 0;
}

static int __init f71882fg_find(int sioaddr, unsigned short *address)
{
	int err = -ENODEV;
	u16 devid;
	u8 start_reg;
	struct f71882fg_data data;

	superio_enter(sioaddr);

	devid = superio_inw(sioaddr, SIO_REG_MANID);
	if (devid != SIO_FINTEK_ID) {
		printk(KERN_INFO DRVNAME ": Not a Fintek device\n");
		goto exit;
	}

	devid = superio_inw(sioaddr, SIO_REG_DEVID);
	if (devid != SIO_F71882_ID) {
		printk(KERN_INFO DRVNAME ": Unsupported Fintek device\n");
		goto exit;
	}

	superio_select(sioaddr, SIO_F71882FG_LD_HWM);
	if (!(superio_inb(sioaddr, SIO_REG_ENABLE) & 0x01)) {
		printk(KERN_WARNING DRVNAME ": Device not activated\n");
		goto exit;
	}

	*address = superio_inw(sioaddr, SIO_REG_ADDR);
	if (*address == 0)
	{
		printk(KERN_WARNING DRVNAME ": Base address not set\n");
		goto exit;
	}
	*address &= ~(REGION_LENGTH - 1);	/* Ignore 3 LSB */

	data.addr = *address;
	start_reg = f71882fg_read8(&data, F71882FG_REG_START);
	if (!(start_reg & 0x03)) {
		printk(KERN_WARNING DRVNAME
			": Hardware monitoring not activated\n");
		goto exit;
	}

	err = 0;
	printk(KERN_INFO DRVNAME ": Found F71882FG chip at %#x, revision %d\n",
		(unsigned int)*address,
		(int)superio_inb(sioaddr, SIO_REG_DEVREV));
exit:
	superio_exit(sioaddr);
	return err;
}

static int __init f71882fg_device_add(unsigned short address)
{
	struct resource res = {
		.start	= address,
		.end	= address + REGION_LENGTH - 1,
		.flags	= IORESOURCE_IO,
	};
	int err;

	f71882fg_pdev = platform_device_alloc(DRVNAME, address);
	if (!f71882fg_pdev)
		return -ENOMEM;

	res.name = f71882fg_pdev->name;
	err = platform_device_add_resources(f71882fg_pdev, &res, 1);
	if (err) {
		printk(KERN_ERR DRVNAME ": Device resource addition failed\n");
		goto exit_device_put;
	}

	err = platform_device_add(f71882fg_pdev);
	if (err) {
		printk(KERN_ERR DRVNAME ": Device addition failed\n");
		goto exit_device_put;
	}

	return 0;

exit_device_put:
	platform_device_put(f71882fg_pdev);

	return err;
}

static int __init f71882fg_init(void)
{
	int err = -ENODEV;
	unsigned short address;

	if (f71882fg_find(0x2e, &address) && f71882fg_find(0x4e, &address))
		goto exit;

	if ((err = platform_driver_register(&f71882fg_driver)))
		goto exit;

	if ((err = f71882fg_device_add(address)))
		goto exit_driver;

	return 0;

exit_driver:
	platform_driver_unregister(&f71882fg_driver);
exit:
	return err;
}

static void __exit f71882fg_exit(void)
{
	platform_device_unregister(f71882fg_pdev);
	platform_driver_unregister(&f71882fg_driver);
}

MODULE_DESCRIPTION("F71882FG Hardware Monitoring Driver");
MODULE_AUTHOR("Hans Edgington (hans@edgington.nl)");
MODULE_LICENSE("GPL");

module_init(f71882fg_init);
module_exit(f71882fg_exit);
