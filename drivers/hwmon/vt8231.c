/*
	vt8231.c - Part of lm_sensors, Linux kernel modules
				for hardware monitoring

	Copyright (c) 2005 Roger Lucas <roger@planbit.co.uk>
	Copyright (c) 2002 Mark D. Studebaker <mdsxyz123@yahoo.com>
			   Aaron M. Marsh <amarsh@sdf.lonestar.org>

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

/* Supports VIA VT8231 South Bridge embedded sensors
*/

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/pci.h>
#include <linux/jiffies.h>
#include <linux/i2c.h>
#include <linux/i2c-isa.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/hwmon-vid.h>
#include <linux/err.h>
#include <asm/io.h>

static int force_addr;
module_param(force_addr, int, 0);
MODULE_PARM_DESC(force_addr, "Initialize the base address of the sensors");

/* Device address
   Note that we can't determine the ISA address until we have initialized
   our module */
static unsigned short isa_address;

#define VT8231_EXTENT 0x80
#define VT8231_BASE_REG 0x70
#define VT8231_ENABLE_REG 0x74

/* The VT8231 registers

   The reset value for the input channel configuration is used (Reg 0x4A=0x07)
   which sets the selected inputs marked with '*' below if multiple options are
   possible:

	            Voltage Mode	  Temperature Mode
	Sensor	      Linux Id	      Linux Id        VIA Id
	--------      --------	      --------        ------
	CPU Diode	N/A		temp1		0
	UIC1		in0		temp2 *		1
	UIC2		in1 *		temp3   	2
	UIC3		in2 *		temp4		3
	UIC4		in3 *		temp5		4
	UIC5		in4 *		temp6		5
	3.3V		in5		N/A

   Note that the BIOS may set the configuration register to a different value
   to match the motherboard configuration.
*/

/* fans numbered 0-1 */
#define VT8231_REG_FAN_MIN(nr)	(0x3b + (nr))
#define VT8231_REG_FAN(nr)	(0x29 + (nr))

/* Voltage inputs numbered 0-5 */

static const u8 regvolt[]    = { 0x21, 0x22, 0x23, 0x24, 0x25, 0x26 };
static const u8 regvoltmax[] = { 0x3d, 0x2b, 0x2d, 0x2f, 0x31, 0x33 };
static const u8 regvoltmin[] = { 0x3e, 0x2c, 0x2e, 0x30, 0x32, 0x34 };

/* Temperatures are numbered 1-6 according to the Linux kernel specification.
**
** In the VIA datasheet, however, the temperatures are numbered from zero.
** Since it is important that this driver can easily be compared to the VIA
** datasheet, we will use the VIA numbering within this driver and map the
** kernel sysfs device name to the VIA number in the sysfs callback.
*/

#define VT8231_REG_TEMP_LOW01	0x49
#define VT8231_REG_TEMP_LOW25	0x4d

static const u8 regtemp[]    = { 0x1f, 0x21, 0x22, 0x23, 0x24, 0x25 };
static const u8 regtempmax[] = { 0x39, 0x3d, 0x2b, 0x2d, 0x2f, 0x31 };
static const u8 regtempmin[] = { 0x3a, 0x3e, 0x2c, 0x2e, 0x30, 0x32 };

#define TEMP_FROM_REG(reg)		(((253 * 4 - (reg)) * 550 + 105) / 210)
#define TEMP_MAXMIN_FROM_REG(reg)	(((253 - (reg)) * 2200 + 105) / 210)
#define TEMP_MAXMIN_TO_REG(val)		(253 - ((val) * 210 + 1100) / 2200)

#define VT8231_REG_CONFIG 0x40
#define VT8231_REG_ALARM1 0x41
#define VT8231_REG_ALARM2 0x42
#define VT8231_REG_FANDIV 0x47
#define VT8231_REG_UCH_CONFIG 0x4a
#define VT8231_REG_TEMP1_CONFIG 0x4b
#define VT8231_REG_TEMP2_CONFIG 0x4c

/* temps 0-5 as numbered in VIA datasheet - see later for mapping to Linux
** numbering
*/
#define ISTEMP(i, ch_config) ((i) == 0 ? 1 : \
			      ((ch_config) >> ((i)+1)) & 0x01)
/* voltages 0-5 */
#define ISVOLT(i, ch_config) ((i) == 5 ? 1 : \
			      !(((ch_config) >> ((i)+2)) & 0x01))

#define DIV_FROM_REG(val) (1 << (val))

/* NB  The values returned here are NOT temperatures.  The calibration curves
**     for the thermistor curves are board-specific and must go in the
**     sensors.conf file.  Temperature sensors are actually ten bits, but the
**     VIA datasheet only considers the 8 MSBs obtained from the regtemp[]
**     register.  The temperature value returned should have a magnitude of 3,
**     so we use the VIA scaling as the "true" scaling and use the remaining 2
**     LSBs as fractional precision.
**
**     All the on-chip hardware temperature comparisons for the alarms are only
**     8-bits wide, and compare against the 8 MSBs of the temperature.  The bits
**     in the registers VT8231_REG_TEMP_LOW01 and VT8231_REG_TEMP_LOW25 are
**     ignored.
*/

/******** FAN RPM CONVERSIONS ********
** This chip saturates back at 0, not at 255 like many the other chips.
** So, 0 means 0 RPM
*/
static inline u8 FAN_TO_REG(long rpm, int div)
{
	if (rpm == 0)
		return 0;
	return SENSORS_LIMIT(1310720 / (rpm * div), 1, 255);
}

#define FAN_FROM_REG(val, div) ((val) == 0 ? 0 : 1310720 / ((val) * (div)))

struct vt8231_data {
	struct i2c_client client;
	struct semaphore update_lock;
	struct class_device *class_dev;
	char valid;		/* !=0 if following fields are valid */
	unsigned long last_updated;	/* In jiffies */

	u8 in[6];		/* Register value */
	u8 in_max[6];		/* Register value */
	u8 in_min[6];		/* Register value */
	u16 temp[6];		/* Register value 10 bit, right aligned */
	u8 temp_max[6];		/* Register value */
	u8 temp_min[6];		/* Register value */
	u8 fan[2];		/* Register value */
	u8 fan_min[2];		/* Register value */
	u8 fan_div[2];		/* Register encoding, shifted right */
	u16 alarms;		/* Register encoding */
	u8 uch_config;
};

static struct pci_dev *s_bridge;
static int vt8231_detect(struct i2c_adapter *adapter);
static int vt8231_detach_client(struct i2c_client *client);
static struct vt8231_data *vt8231_update_device(struct device *dev);
static void vt8231_init_client(struct i2c_client *client);

static inline int vt8231_read_value(struct i2c_client *client, u8 reg)
{
	return inb_p(client->addr + reg);
}

static inline void vt8231_write_value(struct i2c_client *client, u8 reg,
					u8 value)
{
	outb_p(value, client->addr + reg);
}

/* following are the sysfs callback functions */
static ssize_t show_in(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr);
	int nr = sensor_attr->index;
	struct vt8231_data *data = vt8231_update_device(dev);

	return sprintf(buf, "%d\n", ((data->in[nr] - 3) * 10000) / 958);
}

static ssize_t show_in_min(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr);
	int nr = sensor_attr->index;
	struct vt8231_data *data = vt8231_update_device(dev);

	return sprintf(buf, "%d\n", ((data->in_min[nr] - 3) * 10000) / 958);
}

static ssize_t show_in_max(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr);
	int nr = sensor_attr->index;
	struct vt8231_data *data = vt8231_update_device(dev);

	return sprintf(buf, "%d\n", (((data->in_max[nr] - 3) * 10000) / 958));
}

static ssize_t set_in_min(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr);
	int nr = sensor_attr->index;
	struct i2c_client *client = to_i2c_client(dev);
	struct vt8231_data *data = i2c_get_clientdata(client);
	unsigned long val = simple_strtoul(buf, NULL, 10);

	down(&data->update_lock);
	data->in_min[nr] = SENSORS_LIMIT(((val * 958) / 10000) + 3, 0, 255);
	vt8231_write_value(client, regvoltmin[nr], data->in_min[nr]);
	up(&data->update_lock);
	return count;
}

static ssize_t set_in_max(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr);
	int nr = sensor_attr->index;
	struct i2c_client *client = to_i2c_client(dev);
	struct vt8231_data *data = i2c_get_clientdata(client);
	unsigned long val = simple_strtoul(buf, NULL, 10);

	down(&data->update_lock);
	data->in_max[nr] = SENSORS_LIMIT(((val * 958) / 10000) + 3, 0, 255);
	vt8231_write_value(client, regvoltmax[nr], data->in_max[nr]);
	up(&data->update_lock);
	return count;
}

/* Special case for input 5 as this has 3.3V scaling built into the chip */
static ssize_t show_in5(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct vt8231_data *data = vt8231_update_device(dev);

	return sprintf(buf, "%d\n",
		(((data->in[5] - 3) * 10000 * 54) / (958 * 34)));
}

static ssize_t show_in5_min(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct vt8231_data *data = vt8231_update_device(dev);

	return sprintf(buf, "%d\n",
		(((data->in_min[5] - 3) * 10000 * 54) / (958 * 34)));
}

static ssize_t show_in5_max(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct vt8231_data *data = vt8231_update_device(dev);

	return sprintf(buf, "%d\n",
		(((data->in_max[5] - 3) * 10000 * 54) / (958 * 34)));
}

static ssize_t set_in5_min(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct vt8231_data *data = i2c_get_clientdata(client);
	unsigned long val = simple_strtoul(buf, NULL, 10);

	down(&data->update_lock);
	data->in_min[5] = SENSORS_LIMIT(((val * 958 * 34) / (10000 * 54)) + 3,
					0, 255);
	vt8231_write_value(client, regvoltmin[5], data->in_min[5]);
	up(&data->update_lock);
	return count;
}

static ssize_t set_in5_max(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct vt8231_data *data = i2c_get_clientdata(client);
	unsigned long val = simple_strtoul(buf, NULL, 10);

	down(&data->update_lock);
	data->in_max[5] = SENSORS_LIMIT(((val * 958 * 34) / (10000 * 54)) + 3,
					0, 255);
	vt8231_write_value(client, regvoltmax[5], data->in_max[5]);
	up(&data->update_lock);
	return count;
}

#define define_voltage_sysfs(offset)				\
static SENSOR_DEVICE_ATTR(in##offset##_input, S_IRUGO,		\
		show_in, NULL, offset);				\
static SENSOR_DEVICE_ATTR(in##offset##_min, S_IRUGO | S_IWUSR,	\
		show_in_min, set_in_min, offset);		\
static SENSOR_DEVICE_ATTR(in##offset##_max, S_IRUGO | S_IWUSR,	\
		show_in_max, set_in_max, offset)

define_voltage_sysfs(0);
define_voltage_sysfs(1);
define_voltage_sysfs(2);
define_voltage_sysfs(3);
define_voltage_sysfs(4);

static DEVICE_ATTR(in5_input, S_IRUGO, show_in5, NULL);
static DEVICE_ATTR(in5_min, S_IRUGO | S_IWUSR, show_in5_min, set_in5_min);
static DEVICE_ATTR(in5_max, S_IRUGO | S_IWUSR, show_in5_max, set_in5_max);

/* Temperatures */
static ssize_t show_temp0(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct vt8231_data *data = vt8231_update_device(dev);
	return sprintf(buf, "%d\n", data->temp[0] * 250);
}

static ssize_t show_temp0_max(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct vt8231_data *data = vt8231_update_device(dev);
	return sprintf(buf, "%d\n", data->temp_max[0] * 1000);
}

static ssize_t show_temp0_min(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct vt8231_data *data = vt8231_update_device(dev);
	return sprintf(buf, "%d\n", data->temp_min[0] * 1000);
}

static ssize_t set_temp0_max(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct vt8231_data *data = i2c_get_clientdata(client);
	int val = simple_strtol(buf, NULL, 10);

	down(&data->update_lock);
	data->temp_max[0] = SENSORS_LIMIT((val + 500) / 1000, 0, 255);
	vt8231_write_value(client, regtempmax[0], data->temp_max[0]);
	up(&data->update_lock);
	return count;
}
static ssize_t set_temp0_min(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct vt8231_data *data = i2c_get_clientdata(client);
	int val = simple_strtol(buf, NULL, 10);

	down(&data->update_lock);
	data->temp_min[0] = SENSORS_LIMIT((val + 500) / 1000, 0, 255);
	vt8231_write_value(client, regtempmin[0], data->temp_min[0]);
	up(&data->update_lock);
	return count;
}

static ssize_t show_temp(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr);
	int nr = sensor_attr->index;
	struct vt8231_data *data = vt8231_update_device(dev);
	return sprintf(buf, "%d\n", TEMP_FROM_REG(data->temp[nr]));
}

static ssize_t show_temp_max(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr);
	int nr = sensor_attr->index;
	struct vt8231_data *data = vt8231_update_device(dev);
	return sprintf(buf, "%d\n", TEMP_MAXMIN_FROM_REG(data->temp_max[nr]));
}

static ssize_t show_temp_min(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr);
	int nr = sensor_attr->index;
	struct vt8231_data *data = vt8231_update_device(dev);
	return sprintf(buf, "%d\n", TEMP_MAXMIN_FROM_REG(data->temp_min[nr]));
}

static ssize_t set_temp_max(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr);
	int nr = sensor_attr->index;
	struct i2c_client *client = to_i2c_client(dev);
	struct vt8231_data *data = i2c_get_clientdata(client);
	int val = simple_strtol(buf, NULL, 10);

	down(&data->update_lock);
	data->temp_max[nr] = SENSORS_LIMIT(TEMP_MAXMIN_TO_REG(val), 0, 255);
	vt8231_write_value(client, regtempmax[nr], data->temp_max[nr]);
	up(&data->update_lock);
	return count;
}
static ssize_t set_temp_min(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr);
	int nr = sensor_attr->index;
	struct i2c_client *client = to_i2c_client(dev);
	struct vt8231_data *data = i2c_get_clientdata(client);
	int val = simple_strtol(buf, NULL, 10);

	down(&data->update_lock);
	data->temp_min[nr] = SENSORS_LIMIT(TEMP_MAXMIN_TO_REG(val), 0, 255);
	vt8231_write_value(client, regtempmin[nr], data->temp_min[nr]);
	up(&data->update_lock);
	return count;
}

/* Note that these map the Linux temperature sensor numbering (1-6) to the VIA
** temperature sensor numbering (0-5)
*/
#define define_temperature_sysfs(offset)				\
static SENSOR_DEVICE_ATTR(temp##offset##_input, S_IRUGO,		\
		show_temp, NULL, offset - 1);				\
static SENSOR_DEVICE_ATTR(temp##offset##_max, S_IRUGO | S_IWUSR,	\
		show_temp_max, set_temp_max, offset - 1);		\
static SENSOR_DEVICE_ATTR(temp##offset##_max_hyst, S_IRUGO | S_IWUSR,	\
		show_temp_min, set_temp_min, offset - 1)

static DEVICE_ATTR(temp1_input, S_IRUGO, show_temp0, NULL);
static DEVICE_ATTR(temp1_max, S_IRUGO | S_IWUSR, show_temp0_max, set_temp0_max);
static DEVICE_ATTR(temp1_max_hyst, S_IRUGO | S_IWUSR, show_temp0_min, set_temp0_min);

define_temperature_sysfs(2);
define_temperature_sysfs(3);
define_temperature_sysfs(4);
define_temperature_sysfs(5);
define_temperature_sysfs(6);

#define CFG_INFO_TEMP(id)	{ &sensor_dev_attr_temp##id##_input.dev_attr, \
				&sensor_dev_attr_temp##id##_max_hyst.dev_attr, \
				&sensor_dev_attr_temp##id##_max.dev_attr }
#define CFG_INFO_VOLT(id)	{ &sensor_dev_attr_in##id##_input.dev_attr, \
				&sensor_dev_attr_in##id##_min.dev_attr, \
				&sensor_dev_attr_in##id##_max.dev_attr }

struct str_device_attr_table {
	struct device_attribute *input;
	struct device_attribute *min;
	struct device_attribute *max;
};

static struct str_device_attr_table cfg_info_temp[] = {
	{ &dev_attr_temp1_input, &dev_attr_temp1_max_hyst, &dev_attr_temp1_max },
	CFG_INFO_TEMP(2),
	CFG_INFO_TEMP(3),
	CFG_INFO_TEMP(4),
	CFG_INFO_TEMP(5),
	CFG_INFO_TEMP(6)
};

static struct str_device_attr_table cfg_info_volt[] = {
	CFG_INFO_VOLT(0),
	CFG_INFO_VOLT(1),
	CFG_INFO_VOLT(2),
	CFG_INFO_VOLT(3),
	CFG_INFO_VOLT(4),
	{ &dev_attr_in5_input, &dev_attr_in5_min, &dev_attr_in5_max }
};

/* Fans */
static ssize_t show_fan(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr);
	int nr = sensor_attr->index;
	struct vt8231_data *data = vt8231_update_device(dev);
	return sprintf(buf, "%d\n", FAN_FROM_REG(data->fan[nr],
				DIV_FROM_REG(data->fan_div[nr])));
}

static ssize_t show_fan_min(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr);
	int nr = sensor_attr->index;
	struct vt8231_data *data = vt8231_update_device(dev);
	return sprintf(buf, "%d\n", FAN_FROM_REG(data->fan_min[nr],
			DIV_FROM_REG(data->fan_div[nr])));
}

static ssize_t show_fan_div(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr);
	int nr = sensor_attr->index;
	struct vt8231_data *data = vt8231_update_device(dev);
	return sprintf(buf, "%d\n", DIV_FROM_REG(data->fan_div[nr]));
}

static ssize_t set_fan_min(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr);
	int nr = sensor_attr->index;
	struct i2c_client *client = to_i2c_client(dev);
	struct vt8231_data *data = i2c_get_clientdata(client);
	int val = simple_strtoul(buf, NULL, 10);

	down(&data->update_lock);
	data->fan_min[nr] = FAN_TO_REG(val, DIV_FROM_REG(data->fan_div[nr]));
	vt8231_write_value(client, VT8231_REG_FAN_MIN(nr), data->fan_min[nr]);
	up(&data->update_lock);
	return count;
}

static ssize_t set_fan_div(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct vt8231_data *data = i2c_get_clientdata(client);
	struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr);
	unsigned long val = simple_strtoul(buf, NULL, 10);
	int nr = sensor_attr->index;
	int old = vt8231_read_value(client, VT8231_REG_FANDIV);
	long min = FAN_FROM_REG(data->fan_min[nr],
				 DIV_FROM_REG(data->fan_div[nr]));

	down(&data->update_lock);
	switch (val) {
	case 1: data->fan_div[nr] = 0; break;
	case 2: data->fan_div[nr] = 1; break;
	case 4: data->fan_div[nr] = 2; break;
	case 8: data->fan_div[nr] = 3; break;
	default:
		dev_err(&client->dev, "fan_div value %ld not supported."
		        "Choose one of 1, 2, 4 or 8!\n", val);
		up(&data->update_lock);
		return -EINVAL;
	}

	/* Correct the fan minimum speed */
	data->fan_min[nr] = FAN_TO_REG(min, DIV_FROM_REG(data->fan_div[nr]));
	vt8231_write_value(client, VT8231_REG_FAN_MIN(nr), data->fan_min[nr]);

	old = (old & 0x0f) | (data->fan_div[1] << 6) | (data->fan_div[0] << 4);
	vt8231_write_value(client, VT8231_REG_FANDIV, old);
	up(&data->update_lock);
	return count;
}


#define define_fan_sysfs(offset)					\
static SENSOR_DEVICE_ATTR(fan##offset##_input, S_IRUGO,			\
		show_fan, NULL, offset - 1);				\
static SENSOR_DEVICE_ATTR(fan##offset##_div, S_IRUGO | S_IWUSR,		\
		show_fan_div, set_fan_div, offset - 1);			\
static SENSOR_DEVICE_ATTR(fan##offset##_min, S_IRUGO | S_IWUSR,		\
		show_fan_min, set_fan_min, offset - 1)

define_fan_sysfs(1);
define_fan_sysfs(2);

/* Alarms */
static ssize_t show_alarms(struct device *dev, struct device_attribute *attr,
			   char *buf)
{
	struct vt8231_data *data = vt8231_update_device(dev);
	return sprintf(buf, "%d\n", data->alarms);
}

static DEVICE_ATTR(alarms, S_IRUGO, show_alarms, NULL);

static struct i2c_driver vt8231_driver = {
	.driver = {
		.name	= "vt8231",
	},
	.attach_adapter	= vt8231_detect,
	.detach_client	= vt8231_detach_client,
};

static struct pci_device_id vt8231_pci_ids[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_VIA, PCI_DEVICE_ID_VIA_8231_4) },
	{ 0, }
};

MODULE_DEVICE_TABLE(pci, vt8231_pci_ids);

static int __devinit vt8231_pci_probe(struct pci_dev *dev,
			 	      const struct pci_device_id *id);

static struct pci_driver vt8231_pci_driver = {
	.name		= "vt8231",
	.id_table	= vt8231_pci_ids,
	.probe		= vt8231_pci_probe,
};

int vt8231_detect(struct i2c_adapter *adapter)
{
	struct i2c_client *client;
	struct vt8231_data *data;
	int err = 0, i;
	u16 val;

	/* 8231 requires multiple of 256 */
	if (force_addr)	{
		isa_address = force_addr & 0xFF00;
		dev_warn(&adapter->dev, "forcing ISA address 0x%04X\n",
				 isa_address);
		if (PCIBIOS_SUCCESSFUL != pci_write_config_word(s_bridge,
						VT8231_BASE_REG, isa_address))
			return -ENODEV;
	}

	if (PCIBIOS_SUCCESSFUL !=
		pci_read_config_word(s_bridge, VT8231_ENABLE_REG, &val))
		return -ENODEV;

	if (!(val & 0x0001)) {
		dev_warn(&adapter->dev, "enabling sensors\n");
		if (PCIBIOS_SUCCESSFUL !=
			pci_write_config_word(s_bridge, VT8231_ENABLE_REG,
							  val | 0x0001))
			return -ENODEV;
	}

	/* Reserve the ISA region */
	if (!request_region(isa_address, VT8231_EXTENT,
			    vt8231_pci_driver.name)) {
		dev_err(&adapter->dev, "region 0x%x already in use!\n",
			   isa_address);
		return -ENODEV;
	}

	if (!(data = kzalloc(sizeof(struct vt8231_data), GFP_KERNEL))) {
		err = -ENOMEM;
		goto exit_release;
	}

	client = &data->client;
	i2c_set_clientdata(client, data);
	client->addr = isa_address;
	client->adapter = adapter;
	client->driver = &vt8231_driver;
	client->dev.parent = &adapter->dev;

	/* Fill in the remaining client fields and put into the global list */
	strlcpy(client->name, "vt8231", I2C_NAME_SIZE);

	init_MUTEX(&data->update_lock);

	/* Tell the I2C layer a new client has arrived */
	if ((err = i2c_attach_client(client)))
		goto exit_free;

	vt8231_init_client(client);

	/* Register sysfs hooks */
	data->class_dev = hwmon_device_register(&client->dev);
	if (IS_ERR(data->class_dev)) {
		err = PTR_ERR(data->class_dev);
		goto exit_detach;
	}

	/* Must update device information to find out the config field */
	data->uch_config = vt8231_read_value(client, VT8231_REG_UCH_CONFIG);

	for (i = 0; i < ARRAY_SIZE(cfg_info_temp); i++) {
		if (ISTEMP(i, data->uch_config)) {
			device_create_file(&client->dev,
					   cfg_info_temp[i].input);
			device_create_file(&client->dev, cfg_info_temp[i].max);
			device_create_file(&client->dev, cfg_info_temp[i].min);
		}
	}

	for (i = 0; i < ARRAY_SIZE(cfg_info_volt); i++) {
		if (ISVOLT(i, data->uch_config)) {
			device_create_file(&client->dev,
					   cfg_info_volt[i].input);
			device_create_file(&client->dev, cfg_info_volt[i].max);
			device_create_file(&client->dev, cfg_info_volt[i].min);
		}
	}

	device_create_file(&client->dev, &sensor_dev_attr_fan1_input.dev_attr);
	device_create_file(&client->dev, &sensor_dev_attr_fan2_input.dev_attr);
	device_create_file(&client->dev, &sensor_dev_attr_fan1_min.dev_attr);
	device_create_file(&client->dev, &sensor_dev_attr_fan2_min.dev_attr);
	device_create_file(&client->dev, &sensor_dev_attr_fan1_div.dev_attr);
	device_create_file(&client->dev, &sensor_dev_attr_fan2_div.dev_attr);

	device_create_file(&client->dev, &dev_attr_alarms);
	return 0;

exit_detach:
	i2c_detach_client(client);
exit_free:
	kfree(data);
exit_release:
	release_region(isa_address, VT8231_EXTENT);
	return err;
}

static int vt8231_detach_client(struct i2c_client *client)
{
	struct vt8231_data *data = i2c_get_clientdata(client);
	int err;

	hwmon_device_unregister(data->class_dev);

	if ((err = i2c_detach_client(client))) {
		return err;
	}

	release_region(client->addr, VT8231_EXTENT);
	kfree(data);

	return 0;
}

static void vt8231_init_client(struct i2c_client *client)
{
	vt8231_write_value(client, VT8231_REG_TEMP1_CONFIG, 0);
	vt8231_write_value(client, VT8231_REG_TEMP2_CONFIG, 0);
}

static struct vt8231_data *vt8231_update_device(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct vt8231_data *data = i2c_get_clientdata(client);
	int i;
	u16 low;

	down(&data->update_lock);

	if (time_after(jiffies, data->last_updated + HZ + HZ / 2)
	    || !data->valid) {
		for (i = 0; i < 6; i++) {
			if (ISVOLT(i, data->uch_config)) {
				data->in[i] = vt8231_read_value(client,
						regvolt[i]);
				data->in_min[i] = vt8231_read_value(client,
						regvoltmin[i]);
				data->in_max[i] = vt8231_read_value(client,
						regvoltmax[i]);
			}
		}
		for (i = 0; i < 2; i++) {
			data->fan[i] = vt8231_read_value(client,
						VT8231_REG_FAN(i));
			data->fan_min[i] = vt8231_read_value(client,
						VT8231_REG_FAN_MIN(i));
		}

		low = vt8231_read_value(client, VT8231_REG_TEMP_LOW01);
		low = (low >> 6) | ((low & 0x30) >> 2)
		    | (vt8231_read_value(client, VT8231_REG_TEMP_LOW25) << 4);
		for (i = 0; i < 6; i++) {
			if (ISTEMP(i, data->uch_config)) {
				data->temp[i] = (vt8231_read_value(client,
						       regtemp[i]) << 2)
						| ((low >> (2 * i)) & 0x03);
				data->temp_max[i] = vt8231_read_value(client,
						      regtempmax[i]);
				data->temp_min[i] = vt8231_read_value(client,
						      regtempmin[i]);
			}
		}

		i = vt8231_read_value(client, VT8231_REG_FANDIV);
		data->fan_div[0] = (i >> 4) & 0x03;
		data->fan_div[1] = i >> 6;
		data->alarms = vt8231_read_value(client, VT8231_REG_ALARM1) |
			(vt8231_read_value(client, VT8231_REG_ALARM2) << 8);

		/* Set alarm flags correctly */
		if (!data->fan[0] && data->fan_min[0]) {
			data->alarms |= 0x40;
		} else if (data->fan[0] && !data->fan_min[0]) {
			data->alarms &= ~0x40;
		}

		if (!data->fan[1] && data->fan_min[1]) {
			data->alarms |= 0x80;
		} else if (data->fan[1] && !data->fan_min[1]) {
			data->alarms &= ~0x80;
		}

		data->last_updated = jiffies;
		data->valid = 1;
	}

	up(&data->update_lock);

	return data;
}

static int __devinit vt8231_pci_probe(struct pci_dev *dev,
				const struct pci_device_id *id)
{
	u16 val;

	if (PCIBIOS_SUCCESSFUL != pci_read_config_word(dev, VT8231_BASE_REG,
							&val))
		return -ENODEV;

	isa_address = val & ~(VT8231_EXTENT - 1);
	if (isa_address == 0 && force_addr == 0) {
		dev_err(&dev->dev, "base address not set -\
				 upgrade BIOS or use force_addr=0xaddr\n");
		return -ENODEV;
	}

	s_bridge = pci_dev_get(dev);

	if (i2c_isa_add_driver(&vt8231_driver)) {
		pci_dev_put(s_bridge);
		s_bridge = NULL;
	}

	/* Always return failure here.  This is to allow other drivers to bind
	 * to this pci device.  We don't really want to have control over the
	 * pci device, we only wanted to read as few register values from it.
	 */
	return -ENODEV;
}

static int __init sm_vt8231_init(void)
{
	return pci_register_driver(&vt8231_pci_driver);
}

static void __exit sm_vt8231_exit(void)
{
	pci_unregister_driver(&vt8231_pci_driver);
	if (s_bridge != NULL) {
		i2c_isa_del_driver(&vt8231_driver);
		pci_dev_put(s_bridge);
		s_bridge = NULL;
	}
}

MODULE_AUTHOR("Roger Lucas <roger@planbit.co.uk>");
MODULE_DESCRIPTION("VT8231 sensors");
MODULE_LICENSE("GPL");

module_init(sm_vt8231_init);
module_exit(sm_vt8231_exit);
