/*
    w83793.c - Linux kernel driver for hardware monitoring
    Copyright (C) 2006 Winbond Electronics Corp.
                  Yuan Mu
                  Rudolf Marek <r.marek@assembler.cz>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation - version 2.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
    02110-1301 USA.
*/

/*
    Supports following chips:

    Chip	#vin	#fanin	#pwm	#temp	wchipid	vendid	i2c	ISA
    w83793	10	12	8	6	0x7b	0x5ca3	yes	no
*/

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/hwmon.h>
#include <linux/hwmon-vid.h>
#include <linux/hwmon-sysfs.h>
#include <linux/err.h>
#include <linux/mutex.h>

/* Addresses to scan */
static const unsigned short normal_i2c[] = { 0x2c, 0x2d, 0x2e, 0x2f,
						I2C_CLIENT_END };

/* Insmod parameters */
I2C_CLIENT_INSMOD_1(w83793);
I2C_CLIENT_MODULE_PARM(force_subclients, "List of subclient addresses: "
		       "{bus, clientaddr, subclientaddr1, subclientaddr2}");

static int reset;
module_param(reset, bool, 0);
MODULE_PARM_DESC(reset, "Set to 1 to reset chip, not recommended");

/*
   Address 0x00, 0x0d, 0x0e, 0x0f in all three banks are reserved
   as ID, Bank Select registers
*/
#define W83793_REG_BANKSEL		0x00
#define W83793_REG_VENDORID		0x0d
#define W83793_REG_CHIPID		0x0e
#define W83793_REG_DEVICEID		0x0f

#define W83793_REG_CONFIG		0x40
#define W83793_REG_MFC			0x58
#define W83793_REG_FANIN_CTRL		0x5c
#define W83793_REG_FANIN_SEL		0x5d
#define W83793_REG_I2C_ADDR		0x0b
#define W83793_REG_I2C_SUBADDR		0x0c
#define W83793_REG_VID_INA		0x05
#define W83793_REG_VID_INB		0x06
#define W83793_REG_VID_LATCHA		0x07
#define W83793_REG_VID_LATCHB		0x08
#define W83793_REG_VID_CTRL		0x59

static u16 W83793_REG_TEMP_MODE[2] = { 0x5e, 0x5f };

#define TEMP_READ	0
#define TEMP_CRIT	1
#define TEMP_CRIT_HYST	2
#define TEMP_WARN	3
#define TEMP_WARN_HYST	4
/* only crit and crit_hyst affect real-time alarm status
   current crit crit_hyst warn warn_hyst */
static u16 W83793_REG_TEMP[][5] = {
	{0x1c, 0x78, 0x79, 0x7a, 0x7b},
	{0x1d, 0x7c, 0x7d, 0x7e, 0x7f},
	{0x1e, 0x80, 0x81, 0x82, 0x83},
	{0x1f, 0x84, 0x85, 0x86, 0x87},
	{0x20, 0x88, 0x89, 0x8a, 0x8b},
	{0x21, 0x8c, 0x8d, 0x8e, 0x8f},
};

#define W83793_REG_TEMP_LOW_BITS	0x22

#define W83793_REG_BEEP(index)		(0x53 + (index))
#define W83793_REG_ALARM(index)		(0x4b + (index))

#define W83793_REG_CLR_CHASSIS		0x4a	/* SMI MASK4 */
#define W83793_REG_IRQ_CTRL		0x50
#define W83793_REG_OVT_CTRL		0x51
#define W83793_REG_OVT_BEEP		0x52

#define IN_READ				0
#define IN_MAX				1
#define IN_LOW				2
static const u16 W83793_REG_IN[][3] = {
	/* Current, High, Low */
	{0x10, 0x60, 0x61},	/* Vcore A	*/
	{0x11, 0x62, 0x63},	/* Vcore B	*/
	{0x12, 0x64, 0x65},	/* Vtt		*/
	{0x14, 0x6a, 0x6b},	/* VSEN1	*/
	{0x15, 0x6c, 0x6d},	/* VSEN2	*/
	{0x16, 0x6e, 0x6f},	/* +3VSEN	*/
	{0x17, 0x70, 0x71},	/* +12VSEN	*/
	{0x18, 0x72, 0x73},	/* 5VDD		*/
	{0x19, 0x74, 0x75},	/* 5VSB		*/
	{0x1a, 0x76, 0x77},	/* VBAT		*/
};

/* Low Bits of Vcore A/B Vtt Read/High/Low */
static const u16 W83793_REG_IN_LOW_BITS[] = { 0x1b, 0x68, 0x69 };
static u8 scale_in[] = { 2, 2, 2, 16, 16, 16, 8, 24, 24, 16 };
static u8 scale_in_add[] = { 0, 0, 0, 0, 0, 0, 0, 150, 150, 0 };

#define W83793_REG_FAN(index)		(0x23 + 2 * (index))	/* High byte */
#define W83793_REG_FAN_MIN(index)	(0x90 + 2 * (index))	/* High byte */

#define W83793_REG_PWM_DEFAULT		0xb2
#define W83793_REG_PWM_ENABLE		0x207
#define W83793_REG_PWM_UPTIME		0xc3	/* Unit in 0.1 second */
#define W83793_REG_PWM_DOWNTIME		0xc4	/* Unit in 0.1 second */
#define W83793_REG_TEMP_CRITICAL	0xc5

#define PWM_DUTY			0
#define PWM_START			1
#define PWM_NONSTOP			2
#define PWM_STOP_TIME			3
#define W83793_REG_PWM(index, nr)	(((nr) == 0 ? 0xb3 : \
					 (nr) == 1 ? 0x220 : 0x218) + (index))

/* bit field, fan1 is bit0, fan2 is bit1 ... */
#define W83793_REG_TEMP_FAN_MAP(index)	(0x201 + (index))
#define W83793_REG_TEMP_TOL(index)	(0x208 + (index))
#define W83793_REG_TEMP_CRUISE(index)	(0x210 + (index))
#define W83793_REG_PWM_STOP_TIME(index)	(0x228 + (index))
#define W83793_REG_SF2_TEMP(index, nr)	(0x230 + ((index) << 4) + (nr))
#define W83793_REG_SF2_PWM(index, nr)	(0x238 + ((index) << 4) + (nr))

static inline unsigned long FAN_FROM_REG(u16 val)
{
	if ((val >= 0xfff) || (val == 0))
		return	0;
	return (1350000UL / val);
}

static inline u16 FAN_TO_REG(long rpm)
{
	if (rpm <= 0)
		return 0x0fff;
	return SENSORS_LIMIT((1350000 + (rpm >> 1)) / rpm, 1, 0xffe);
}

static inline unsigned long TIME_FROM_REG(u8 reg)
{
	return (reg * 100);
}

static inline u8 TIME_TO_REG(unsigned long val)
{
	return SENSORS_LIMIT((val + 50) / 100, 0, 0xff);
}

static inline long TEMP_FROM_REG(s8 reg)
{
	return (reg * 1000);
}

static inline s8 TEMP_TO_REG(long val, s8 min, s8 max)
{
	return SENSORS_LIMIT((val + (val < 0 ? -500 : 500)) / 1000, min, max);
}

struct w83793_data {
	struct i2c_client client;
	struct i2c_client *lm75[2];
	struct device *hwmon_dev;
	struct mutex update_lock;
	char valid;			/* !=0 if following fields are valid */
	unsigned long last_updated;	/* In jiffies */
	unsigned long last_nonvolatile;	/* In jiffies, last time we update the
					   nonvolatile registers */

	u8 bank;
	u8 vrm;
	u8 vid[2];
	u8 in[10][3];		/* Register value, read/high/low */
	u8 in_low_bits[3];	/* Additional resolution for VCore A/B Vtt */

	u16 has_fan;		/* Only fan1- fan5 has own pins */
	u16 fan[12];		/* Register value combine */
	u16 fan_min[12];	/* Register value combine */

	s8 temp[6][5];		/* current, crit, crit_hyst,warn, warn_hyst */
	u8 temp_low_bits;	/* Additional resolution TD1-TD4 */
	u8 temp_mode[2];	/* byte 0: Temp D1-D4 mode each has 2 bits
				   byte 1: Temp R1,R2 mode, each has 1 bit */
	u8 temp_critical;	/* If reached all fan will be at full speed */
	u8 temp_fan_map[6];	/* Temp controls which pwm fan, bit field */

	u8 has_pwm;
	u8 has_temp;
	u8 has_vid;
	u8 pwm_enable;		/* Register value, each Temp has 1 bit */
	u8 pwm_uptime;		/* Register value */
	u8 pwm_downtime;	/* Register value */
	u8 pwm_default;		/* All fan default pwm, next poweron valid */
	u8 pwm[8][3];		/* Register value */
	u8 pwm_stop_time[8];
	u8 temp_cruise[6];

	u8 alarms[5];		/* realtime status registers */
	u8 beeps[5];
	u8 beep_enable;
	u8 tolerance[3];	/* Temp tolerance(Smart Fan I/II) */
	u8 sf2_pwm[6][7];	/* Smart FanII: Fan duty cycle */
	u8 sf2_temp[6][7];	/* Smart FanII: Temp level point */
};

static u8 w83793_read_value(struct i2c_client *client, u16 reg);
static int w83793_write_value(struct i2c_client *client, u16 reg, u8 value);
static int w83793_attach_adapter(struct i2c_adapter *adapter);
static int w83793_detect(struct i2c_adapter *adapter, int address, int kind);
static int w83793_detach_client(struct i2c_client *client);
static void w83793_init_client(struct i2c_client *client);
static void w83793_update_nonvolatile(struct device *dev);
static struct w83793_data *w83793_update_device(struct device *dev);

static struct i2c_driver w83793_driver = {
	.driver = {
		   .name = "w83793",
	},
	.attach_adapter = w83793_attach_adapter,
	.detach_client = w83793_detach_client,
};

static ssize_t
show_vrm(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct w83793_data *data = dev_get_drvdata(dev);
	return sprintf(buf, "%d\n", data->vrm);
}

static ssize_t
show_vid(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct w83793_data *data = w83793_update_device(dev);
	struct sensor_device_attribute_2 *sensor_attr =
	    to_sensor_dev_attr_2(attr);
	int index = sensor_attr->index;

	return sprintf(buf, "%d\n", vid_from_reg(data->vid[index], data->vrm));
}

static ssize_t
store_vrm(struct device *dev, struct device_attribute *attr,
	  const char *buf, size_t count)
{
	struct w83793_data *data = dev_get_drvdata(dev);
	data->vrm = simple_strtoul(buf, NULL, 10);
	return count;
}

#define ALARM_STATUS			0
#define BEEP_ENABLE			1
static ssize_t
show_alarm_beep(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct w83793_data *data = w83793_update_device(dev);
	struct sensor_device_attribute_2 *sensor_attr =
	    to_sensor_dev_attr_2(attr);
	int nr = sensor_attr->nr;
	int index = sensor_attr->index >> 3;
	int bit = sensor_attr->index & 0x07;
	u8 val;

	if (ALARM_STATUS == nr) {
		val = (data->alarms[index] >> (bit)) & 1;
	} else {		/* BEEP_ENABLE */
		val = (data->beeps[index] >> (bit)) & 1;
	}

	return sprintf(buf, "%u\n", val);
}

static ssize_t
store_beep(struct device *dev, struct device_attribute *attr,
	   const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct w83793_data *data = i2c_get_clientdata(client);
	struct sensor_device_attribute_2 *sensor_attr =
	    to_sensor_dev_attr_2(attr);
	int index = sensor_attr->index >> 3;
	int shift = sensor_attr->index & 0x07;
	u8 beep_bit = 1 << shift;
	u8 val;

	val = simple_strtoul(buf, NULL, 10);
	if (val != 0 && val != 1)
		return -EINVAL;

	mutex_lock(&data->update_lock);
	data->beeps[index] = w83793_read_value(client, W83793_REG_BEEP(index));
	data->beeps[index] &= ~beep_bit;
	data->beeps[index] |= val << shift;
	w83793_write_value(client, W83793_REG_BEEP(index), data->beeps[index]);
	mutex_unlock(&data->update_lock);

	return count;
}

static ssize_t
show_beep_enable(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct w83793_data *data = w83793_update_device(dev);
	return sprintf(buf, "%u\n", (data->beep_enable >> 1) & 0x01);
}

static ssize_t
store_beep_enable(struct device *dev, struct device_attribute *attr,
		  const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct w83793_data *data = i2c_get_clientdata(client);
	u8 val = simple_strtoul(buf, NULL, 10);

	if (val != 0 && val != 1)
		return -EINVAL;

	mutex_lock(&data->update_lock);
	data->beep_enable = w83793_read_value(client, W83793_REG_OVT_BEEP)
			    & 0xfd;
	data->beep_enable |= val << 1;
	w83793_write_value(client, W83793_REG_OVT_BEEP, data->beep_enable);
	mutex_unlock(&data->update_lock);

	return count;
}

/* Write any value to clear chassis alarm */
static ssize_t
store_chassis_clear(struct device *dev,
		    struct device_attribute *attr, const char *buf,
		    size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct w83793_data *data = i2c_get_clientdata(client);
	u8 val;

	mutex_lock(&data->update_lock);
	val = w83793_read_value(client, W83793_REG_CLR_CHASSIS);
	val |= 0x80;
	w83793_write_value(client, W83793_REG_CLR_CHASSIS, val);
	mutex_unlock(&data->update_lock);
	return count;
}

#define FAN_INPUT			0
#define FAN_MIN				1
static ssize_t
show_fan(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct sensor_device_attribute_2 *sensor_attr =
	    to_sensor_dev_attr_2(attr);
	int nr = sensor_attr->nr;
	int index = sensor_attr->index;
	struct w83793_data *data = w83793_update_device(dev);
	u16 val;

	if (FAN_INPUT == nr) {
		val = data->fan[index] & 0x0fff;
	} else {
		val = data->fan_min[index] & 0x0fff;
	}

	return sprintf(buf, "%lu\n", FAN_FROM_REG(val));
}

static ssize_t
store_fan_min(struct device *dev, struct device_attribute *attr,
	      const char *buf, size_t count)
{
	struct sensor_device_attribute_2 *sensor_attr =
	    to_sensor_dev_attr_2(attr);
	int index = sensor_attr->index;
	struct i2c_client *client = to_i2c_client(dev);
	struct w83793_data *data = i2c_get_clientdata(client);
	u16 val = FAN_TO_REG(simple_strtoul(buf, NULL, 10));

	mutex_lock(&data->update_lock);
	data->fan_min[index] = val;
	w83793_write_value(client, W83793_REG_FAN_MIN(index),
			   (val >> 8) & 0xff);
	w83793_write_value(client, W83793_REG_FAN_MIN(index) + 1, val & 0xff);
	mutex_unlock(&data->update_lock);

	return count;
}

static ssize_t
show_pwm(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct sensor_device_attribute_2 *sensor_attr =
	    to_sensor_dev_attr_2(attr);
	struct w83793_data *data = w83793_update_device(dev);
	u16 val;
	int nr = sensor_attr->nr;
	int index = sensor_attr->index;

	if (PWM_STOP_TIME == nr)
		val = TIME_FROM_REG(data->pwm_stop_time[index]);
	else
		val = (data->pwm[index][nr] & 0x3f) << 2;

	return sprintf(buf, "%d\n", val);
}

static ssize_t
store_pwm(struct device *dev, struct device_attribute *attr,
	  const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct w83793_data *data = i2c_get_clientdata(client);
	struct sensor_device_attribute_2 *sensor_attr =
	    to_sensor_dev_attr_2(attr);
	int nr = sensor_attr->nr;
	int index = sensor_attr->index;
	u8 val;

	mutex_lock(&data->update_lock);
	if (PWM_STOP_TIME == nr) {
		val = TIME_TO_REG(simple_strtoul(buf, NULL, 10));
		data->pwm_stop_time[index] = val;
		w83793_write_value(client, W83793_REG_PWM_STOP_TIME(index),
				   val);
	} else {
		val = SENSORS_LIMIT(simple_strtoul(buf, NULL, 10), 0, 0xff)
		      >> 2;
		data->pwm[index][nr] =
		    w83793_read_value(client, W83793_REG_PWM(index, nr)) & 0xc0;
		data->pwm[index][nr] |= val;
		w83793_write_value(client, W83793_REG_PWM(index, nr),
							data->pwm[index][nr]);
	}

	mutex_unlock(&data->update_lock);
	return count;
}

static ssize_t
show_temp(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct sensor_device_attribute_2 *sensor_attr =
	    to_sensor_dev_attr_2(attr);
	int nr = sensor_attr->nr;
	int index = sensor_attr->index;
	struct w83793_data *data = w83793_update_device(dev);
	long temp = TEMP_FROM_REG(data->temp[index][nr]);

	if (TEMP_READ == nr && index < 4) {	/* Only TD1-TD4 have low bits */
		int low = ((data->temp_low_bits >> (index * 2)) & 0x03) * 250;
		temp += temp > 0 ? low : -low;
	}
	return sprintf(buf, "%ld\n", temp);
}

static ssize_t
store_temp(struct device *dev, struct device_attribute *attr,
	   const char *buf, size_t count)
{
	struct sensor_device_attribute_2 *sensor_attr =
	    to_sensor_dev_attr_2(attr);
	int nr = sensor_attr->nr;
	int index = sensor_attr->index;
	struct i2c_client *client = to_i2c_client(dev);
	struct w83793_data *data = i2c_get_clientdata(client);
	long tmp = simple_strtol(buf, NULL, 10);

	mutex_lock(&data->update_lock);
	data->temp[index][nr] = TEMP_TO_REG(tmp, -128, 127);
	w83793_write_value(client, W83793_REG_TEMP[index][nr],
			   data->temp[index][nr]);
	mutex_unlock(&data->update_lock);
	return count;
}

/*
	TD1-TD4
	each has 4 mode:(2 bits)
	0:	Stop monitor
	1:	Use internal temp sensor(default)
	2:	Reserved
	3:	Use sensor in Intel CPU and get result by PECI

	TR1-TR2
	each has 2 mode:(1 bit)
	0:	Disable temp sensor monitor
	1:	To enable temp sensors monitor
*/

/* 0 disable, 6 PECI */
static u8 TO_TEMP_MODE[] = { 0, 0, 0, 6 };

static ssize_t
show_temp_mode(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct w83793_data *data = w83793_update_device(dev);
	struct sensor_device_attribute_2 *sensor_attr =
	    to_sensor_dev_attr_2(attr);
	int index = sensor_attr->index;
	u8 mask = (index < 4) ? 0x03 : 0x01;
	u8 shift = (index < 4) ? (2 * index) : (index - 4);
	u8 tmp;
	index = (index < 4) ? 0 : 1;

	tmp = (data->temp_mode[index] >> shift) & mask;

	/* for the internal sensor, found out if diode or thermistor */
	if (tmp == 1) {
		tmp = index == 0 ? 3 : 4;
	} else {
		tmp = TO_TEMP_MODE[tmp];
	}

	return sprintf(buf, "%d\n", tmp);
}

static ssize_t
store_temp_mode(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct w83793_data *data = i2c_get_clientdata(client);
	struct sensor_device_attribute_2 *sensor_attr =
	    to_sensor_dev_attr_2(attr);
	int index = sensor_attr->index;
	u8 mask = (index < 4) ? 0x03 : 0x01;
	u8 shift = (index < 4) ? (2 * index) : (index - 4);
	u8 val = simple_strtoul(buf, NULL, 10);

	/* transform the sysfs interface values into table above */
	if ((val == 6) && (index < 4)) {
		val -= 3;
	} else if ((val == 3 && index < 4)
		|| (val == 4 && index >= 4)) {
		/* transform diode or thermistor into internal enable */
		val = !!val;
	} else {
		return -EINVAL;
	}

	index = (index < 4) ? 0 : 1;
	mutex_lock(&data->update_lock);
	data->temp_mode[index] =
	    w83793_read_value(client, W83793_REG_TEMP_MODE[index]);
	data->temp_mode[index] &= ~(mask << shift);
	data->temp_mode[index] |= val << shift;
	w83793_write_value(client, W83793_REG_TEMP_MODE[index],
							data->temp_mode[index]);
	mutex_unlock(&data->update_lock);

	return count;
}

#define SETUP_PWM_DEFAULT		0
#define SETUP_PWM_UPTIME		1	/* Unit in 0.1s */
#define SETUP_PWM_DOWNTIME		2	/* Unit in 0.1s */
#define SETUP_TEMP_CRITICAL		3
static ssize_t
show_sf_setup(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct sensor_device_attribute_2 *sensor_attr =
	    to_sensor_dev_attr_2(attr);
	int nr = sensor_attr->nr;
	struct w83793_data *data = w83793_update_device(dev);
	u32 val = 0;

	if (SETUP_PWM_DEFAULT == nr) {
		val = (data->pwm_default & 0x3f) << 2;
	} else if (SETUP_PWM_UPTIME == nr) {
		val = TIME_FROM_REG(data->pwm_uptime);
	} else if (SETUP_PWM_DOWNTIME == nr) {
		val = TIME_FROM_REG(data->pwm_downtime);
	} else if (SETUP_TEMP_CRITICAL == nr) {
		val = TEMP_FROM_REG(data->temp_critical & 0x7f);
	}

	return sprintf(buf, "%d\n", val);
}

static ssize_t
store_sf_setup(struct device *dev, struct device_attribute *attr,
	       const char *buf, size_t count)
{
	struct sensor_device_attribute_2 *sensor_attr =
	    to_sensor_dev_attr_2(attr);
	int nr = sensor_attr->nr;
	struct i2c_client *client = to_i2c_client(dev);
	struct w83793_data *data = i2c_get_clientdata(client);

	mutex_lock(&data->update_lock);
	if (SETUP_PWM_DEFAULT == nr) {
		data->pwm_default =
		    w83793_read_value(client, W83793_REG_PWM_DEFAULT) & 0xc0;
		data->pwm_default |= SENSORS_LIMIT(simple_strtoul(buf, NULL,
								  10),
						   0, 0xff) >> 2;
		w83793_write_value(client, W83793_REG_PWM_DEFAULT,
							data->pwm_default);
	} else if (SETUP_PWM_UPTIME == nr) {
		data->pwm_uptime = TIME_TO_REG(simple_strtoul(buf, NULL, 10));
		data->pwm_uptime += data->pwm_uptime == 0 ? 1 : 0;
		w83793_write_value(client, W83793_REG_PWM_UPTIME,
							data->pwm_uptime);
	} else if (SETUP_PWM_DOWNTIME == nr) {
		data->pwm_downtime = TIME_TO_REG(simple_strtoul(buf, NULL, 10));
		data->pwm_downtime += data->pwm_downtime == 0 ? 1 : 0;
		w83793_write_value(client, W83793_REG_PWM_DOWNTIME,
							data->pwm_downtime);
	} else {		/* SETUP_TEMP_CRITICAL */
		data->temp_critical =
		    w83793_read_value(client, W83793_REG_TEMP_CRITICAL) & 0x80;
		data->temp_critical |= TEMP_TO_REG(simple_strtol(buf, NULL, 10),
						   0, 0x7f);
		w83793_write_value(client, W83793_REG_TEMP_CRITICAL,
							data->temp_critical);
	}

	mutex_unlock(&data->update_lock);
	return count;
}

/*
	Temp SmartFan control
	TEMP_FAN_MAP
	Temp channel control which pwm fan, bitfield, bit 0 indicate pwm1...
	It's possible two or more temp channels control the same fan, w83793
	always prefers to pick the most critical request and applies it to
	the related Fan.
	It's possible one fan is not in any mapping of 6 temp channels, this
	means the fan is manual mode

	TEMP_PWM_ENABLE
	Each temp channel has its own SmartFan mode, and temp channel
	control	fans that are set by TEMP_FAN_MAP
	0:	SmartFanII mode
	1:	Thermal Cruise Mode

	TEMP_CRUISE
	Target temperature in thermal cruise mode, w83793 will try to turn
	fan speed to keep the temperature of target device around this
	temperature.

	TEMP_TOLERANCE
	If Temp higher or lower than target with this tolerance, w83793
	will take actions to speed up or slow down the fan to keep the
	temperature within the tolerance range.
*/

#define TEMP_FAN_MAP			0
#define TEMP_PWM_ENABLE			1
#define TEMP_CRUISE			2
#define TEMP_TOLERANCE			3
static ssize_t
show_sf_ctrl(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct sensor_device_attribute_2 *sensor_attr =
	    to_sensor_dev_attr_2(attr);
	int nr = sensor_attr->nr;
	int index = sensor_attr->index;
	struct w83793_data *data = w83793_update_device(dev);
	u32 val;

	if (TEMP_FAN_MAP == nr) {
		val = data->temp_fan_map[index];
	} else if (TEMP_PWM_ENABLE == nr) {
		/* +2 to transfrom into 2 and 3 to conform with sysfs intf */
		val = ((data->pwm_enable >> index) & 0x01) + 2;
	} else if (TEMP_CRUISE == nr) {
		val = TEMP_FROM_REG(data->temp_cruise[index] & 0x7f);
	} else {		/* TEMP_TOLERANCE */
		val = data->tolerance[index >> 1] >> ((index & 0x01) ? 4 : 0);
		val = TEMP_FROM_REG(val & 0x0f);
	}
	return sprintf(buf, "%d\n", val);
}

static ssize_t
store_sf_ctrl(struct device *dev, struct device_attribute *attr,
	      const char *buf, size_t count)
{
	struct sensor_device_attribute_2 *sensor_attr =
	    to_sensor_dev_attr_2(attr);
	int nr = sensor_attr->nr;
	int index = sensor_attr->index;
	struct i2c_client *client = to_i2c_client(dev);
	struct w83793_data *data = i2c_get_clientdata(client);
	u32 val;

	mutex_lock(&data->update_lock);
	if (TEMP_FAN_MAP == nr) {
		val = simple_strtoul(buf, NULL, 10) & 0xff;
		w83793_write_value(client, W83793_REG_TEMP_FAN_MAP(index), val);
		data->temp_fan_map[index] = val;
	} else if (TEMP_PWM_ENABLE == nr) {
		val = simple_strtoul(buf, NULL, 10);
		if (2 == val || 3 == val) {
			data->pwm_enable =
			    w83793_read_value(client, W83793_REG_PWM_ENABLE);
			if (val - 2)
				data->pwm_enable |= 1 << index;
			else
				data->pwm_enable &= ~(1 << index);
			w83793_write_value(client, W83793_REG_PWM_ENABLE,
							data->pwm_enable);
		} else {
			mutex_unlock(&data->update_lock);
			return -EINVAL;
		}
	} else if (TEMP_CRUISE == nr) {
		data->temp_cruise[index] =
		    w83793_read_value(client, W83793_REG_TEMP_CRUISE(index));
		val = TEMP_TO_REG(simple_strtol(buf, NULL, 10), 0, 0x7f);
		data->temp_cruise[index] &= 0x80;
		data->temp_cruise[index] |= val;

		w83793_write_value(client, W83793_REG_TEMP_CRUISE(index),
						data->temp_cruise[index]);
	} else {		/* TEMP_TOLERANCE */
		int i = index >> 1;
		u8 shift = (index & 0x01) ? 4 : 0;
		data->tolerance[i] =
		    w83793_read_value(client, W83793_REG_TEMP_TOL(i));

		val = TEMP_TO_REG(simple_strtol(buf, NULL, 10), 0, 0x0f);
		data->tolerance[i] &= ~(0x0f << shift);
		data->tolerance[i] |= val << shift;
		w83793_write_value(client, W83793_REG_TEMP_TOL(i),
							data->tolerance[i]);
	}

	mutex_unlock(&data->update_lock);
	return count;
}

static ssize_t
show_sf2_pwm(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct sensor_device_attribute_2 *sensor_attr =
	    to_sensor_dev_attr_2(attr);
	int nr = sensor_attr->nr;
	int index = sensor_attr->index;
	struct w83793_data *data = w83793_update_device(dev);

	return sprintf(buf, "%d\n", (data->sf2_pwm[index][nr] & 0x3f) << 2);
}

static ssize_t
store_sf2_pwm(struct device *dev, struct device_attribute *attr,
	      const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct w83793_data *data = i2c_get_clientdata(client);
	struct sensor_device_attribute_2 *sensor_attr =
	    to_sensor_dev_attr_2(attr);
	int nr = sensor_attr->nr;
	int index = sensor_attr->index;
	u8 val = SENSORS_LIMIT(simple_strtoul(buf, NULL, 10), 0, 0xff) >> 2;

	mutex_lock(&data->update_lock);
	data->sf2_pwm[index][nr] =
	    w83793_read_value(client, W83793_REG_SF2_PWM(index, nr)) & 0xc0;
	data->sf2_pwm[index][nr] |= val;
	w83793_write_value(client, W83793_REG_SF2_PWM(index, nr),
						data->sf2_pwm[index][nr]);
	mutex_unlock(&data->update_lock);
	return count;
}

static ssize_t
show_sf2_temp(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct sensor_device_attribute_2 *sensor_attr =
	    to_sensor_dev_attr_2(attr);
	int nr = sensor_attr->nr;
	int index = sensor_attr->index;
	struct w83793_data *data = w83793_update_device(dev);

	return sprintf(buf, "%ld\n",
		       TEMP_FROM_REG(data->sf2_temp[index][nr] & 0x7f));
}

static ssize_t
store_sf2_temp(struct device *dev, struct device_attribute *attr,
	       const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct w83793_data *data = i2c_get_clientdata(client);
	struct sensor_device_attribute_2 *sensor_attr =
	    to_sensor_dev_attr_2(attr);
	int nr = sensor_attr->nr;
	int index = sensor_attr->index;
	u8 val = TEMP_TO_REG(simple_strtol(buf, NULL, 10), 0, 0x7f);

	mutex_lock(&data->update_lock);
	data->sf2_temp[index][nr] =
	    w83793_read_value(client, W83793_REG_SF2_TEMP(index, nr)) & 0x80;
	data->sf2_temp[index][nr] |= val;
	w83793_write_value(client, W83793_REG_SF2_TEMP(index, nr),
					     data->sf2_temp[index][nr]);
	mutex_unlock(&data->update_lock);
	return count;
}

/* only Vcore A/B and Vtt have additional 2 bits precision */
static ssize_t
show_in(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct sensor_device_attribute_2 *sensor_attr =
	    to_sensor_dev_attr_2(attr);
	int nr = sensor_attr->nr;
	int index = sensor_attr->index;
	struct w83793_data *data = w83793_update_device(dev);
	u16 val = data->in[index][nr];

	if (index < 3) {
		val <<= 2;
		val += (data->in_low_bits[nr] >> (index * 2)) & 0x3;
	}
	/* voltage inputs 5VDD and 5VSB needs 150mV offset */
	val = val * scale_in[index] + scale_in_add[index];
	return sprintf(buf, "%d\n", val);
}

static ssize_t
store_in(struct device *dev, struct device_attribute *attr,
	 const char *buf, size_t count)
{
	struct sensor_device_attribute_2 *sensor_attr =
	    to_sensor_dev_attr_2(attr);
	int nr = sensor_attr->nr;
	int index = sensor_attr->index;
	struct i2c_client *client = to_i2c_client(dev);
	struct w83793_data *data = i2c_get_clientdata(client);
	u32 val;

	val =
	    (simple_strtoul(buf, NULL, 10) +
	     scale_in[index] / 2) / scale_in[index];
	mutex_lock(&data->update_lock);
	if (index > 2) {
		/* fix the limit values of 5VDD and 5VSB to ALARM mechanism */
		if (1 == nr || 2 == nr) {
			val -= scale_in_add[index] / scale_in[index];
		}
		val = SENSORS_LIMIT(val, 0, 255);
	} else {
		val = SENSORS_LIMIT(val, 0, 0x3FF);
		data->in_low_bits[nr] =
		    w83793_read_value(client, W83793_REG_IN_LOW_BITS[nr]);
		data->in_low_bits[nr] &= ~(0x03 << (2 * index));
		data->in_low_bits[nr] |= (val & 0x03) << (2 * index);
		w83793_write_value(client, W83793_REG_IN_LOW_BITS[nr],
						     data->in_low_bits[nr]);
		val >>= 2;
	}
	data->in[index][nr] = val;
	w83793_write_value(client, W83793_REG_IN[index][nr],
							data->in[index][nr]);
	mutex_unlock(&data->update_lock);
	return count;
}

#define NOT_USED			-1

#define SENSOR_ATTR_IN(index)						\
	SENSOR_ATTR_2(in##index##_input, S_IRUGO, show_in, NULL,	\
		IN_READ, index),					\
	SENSOR_ATTR_2(in##index##_max, S_IRUGO | S_IWUSR, show_in,	\
		store_in, IN_MAX, index),				\
	SENSOR_ATTR_2(in##index##_min, S_IRUGO | S_IWUSR, show_in,	\
		store_in, IN_LOW, index),				\
	SENSOR_ATTR_2(in##index##_alarm, S_IRUGO, show_alarm_beep,	\
		NULL, ALARM_STATUS, index + ((index > 2) ? 1 : 0)),	\
	SENSOR_ATTR_2(in##index##_beep, S_IWUSR | S_IRUGO,		\
		show_alarm_beep, store_beep, BEEP_ENABLE,		\
		index + ((index > 2) ? 1 : 0))

#define SENSOR_ATTR_FAN(index)						\
	SENSOR_ATTR_2(fan##index##_alarm, S_IRUGO, show_alarm_beep,	\
		NULL, ALARM_STATUS, index + 17),			\
	SENSOR_ATTR_2(fan##index##_beep, S_IWUSR | S_IRUGO,		\
		show_alarm_beep, store_beep, BEEP_ENABLE, index + 17),	\
	SENSOR_ATTR_2(fan##index##_input, S_IRUGO, show_fan,		\
		NULL, FAN_INPUT, index - 1),				\
	SENSOR_ATTR_2(fan##index##_min, S_IWUSR | S_IRUGO,		\
		show_fan, store_fan_min, FAN_MIN, index - 1)

#define SENSOR_ATTR_PWM(index)						\
	SENSOR_ATTR_2(pwm##index, S_IWUSR | S_IRUGO, show_pwm,		\
		store_pwm, PWM_DUTY, index - 1),			\
	SENSOR_ATTR_2(pwm##index##_nonstop, S_IWUSR | S_IRUGO,		\
		show_pwm, store_pwm, PWM_NONSTOP, index - 1),		\
	SENSOR_ATTR_2(pwm##index##_start, S_IWUSR | S_IRUGO,		\
		show_pwm, store_pwm, PWM_START, index - 1),		\
	SENSOR_ATTR_2(pwm##index##_stop_time, S_IWUSR | S_IRUGO,	\
		show_pwm, store_pwm, PWM_STOP_TIME, index - 1)

#define SENSOR_ATTR_TEMP(index)						\
	SENSOR_ATTR_2(temp##index##_type, S_IRUGO | S_IWUSR,		\
		show_temp_mode, store_temp_mode, NOT_USED, index - 1),	\
	SENSOR_ATTR_2(temp##index##_input, S_IRUGO, show_temp,		\
		NULL, TEMP_READ, index - 1),				\
	SENSOR_ATTR_2(temp##index##_max, S_IRUGO | S_IWUSR, show_temp,	\
		store_temp, TEMP_CRIT, index - 1),			\
	SENSOR_ATTR_2(temp##index##_max_hyst, S_IRUGO | S_IWUSR,	\
		show_temp, store_temp, TEMP_CRIT_HYST, index - 1),	\
	SENSOR_ATTR_2(temp##index##_warn, S_IRUGO | S_IWUSR, show_temp,	\
		store_temp, TEMP_WARN, index - 1),			\
	SENSOR_ATTR_2(temp##index##_warn_hyst, S_IRUGO | S_IWUSR,	\
		show_temp, store_temp, TEMP_WARN_HYST, index - 1),	\
	SENSOR_ATTR_2(temp##index##_alarm, S_IRUGO,			\
		show_alarm_beep, NULL, ALARM_STATUS, index + 11),	\
	SENSOR_ATTR_2(temp##index##_beep, S_IWUSR | S_IRUGO,		\
		show_alarm_beep, store_beep, BEEP_ENABLE, index + 11),	\
	SENSOR_ATTR_2(temp##index##_auto_channels_pwm,			\
		S_IRUGO | S_IWUSR, show_sf_ctrl, store_sf_ctrl,		\
		TEMP_FAN_MAP, index - 1),				\
	SENSOR_ATTR_2(temp##index##_pwm_enable, S_IWUSR | S_IRUGO,	\
		show_sf_ctrl, store_sf_ctrl, TEMP_PWM_ENABLE,		\
		index - 1),						\
	SENSOR_ATTR_2(thermal_cruise##index, S_IRUGO | S_IWUSR,		\
		show_sf_ctrl, store_sf_ctrl, TEMP_CRUISE, index - 1),	\
	SENSOR_ATTR_2(tolerance##index, S_IRUGO | S_IWUSR, show_sf_ctrl,\
		store_sf_ctrl, TEMP_TOLERANCE, index - 1),		\
	SENSOR_ATTR_2(temp##index##_auto_point1_pwm, S_IRUGO | S_IWUSR, \
		show_sf2_pwm, store_sf2_pwm, 0, index - 1),		\
	SENSOR_ATTR_2(temp##index##_auto_point2_pwm, S_IRUGO | S_IWUSR, \
		show_sf2_pwm, store_sf2_pwm, 1, index - 1),		\
	SENSOR_ATTR_2(temp##index##_auto_point3_pwm, S_IRUGO | S_IWUSR, \
		show_sf2_pwm, store_sf2_pwm, 2, index - 1),		\
	SENSOR_ATTR_2(temp##index##_auto_point4_pwm, S_IRUGO | S_IWUSR, \
		show_sf2_pwm, store_sf2_pwm, 3, index - 1),		\
	SENSOR_ATTR_2(temp##index##_auto_point5_pwm, S_IRUGO | S_IWUSR, \
		show_sf2_pwm, store_sf2_pwm, 4, index - 1),		\
	SENSOR_ATTR_2(temp##index##_auto_point6_pwm, S_IRUGO | S_IWUSR, \
		show_sf2_pwm, store_sf2_pwm, 5, index - 1),		\
	SENSOR_ATTR_2(temp##index##_auto_point7_pwm, S_IRUGO | S_IWUSR, \
		show_sf2_pwm, store_sf2_pwm, 6, index - 1),		\
	SENSOR_ATTR_2(temp##index##_auto_point1_temp, S_IRUGO | S_IWUSR,\
		show_sf2_temp, store_sf2_temp, 0, index - 1),		\
	SENSOR_ATTR_2(temp##index##_auto_point2_temp, S_IRUGO | S_IWUSR,\
		show_sf2_temp, store_sf2_temp, 1, index - 1),		\
	SENSOR_ATTR_2(temp##index##_auto_point3_temp, S_IRUGO | S_IWUSR,\
		show_sf2_temp, store_sf2_temp, 2, index - 1),		\
	SENSOR_ATTR_2(temp##index##_auto_point4_temp, S_IRUGO | S_IWUSR,\
		show_sf2_temp, store_sf2_temp, 3, index - 1),		\
	SENSOR_ATTR_2(temp##index##_auto_point5_temp, S_IRUGO | S_IWUSR,\
		show_sf2_temp, store_sf2_temp, 4, index - 1),		\
	SENSOR_ATTR_2(temp##index##_auto_point6_temp, S_IRUGO | S_IWUSR,\
		show_sf2_temp, store_sf2_temp, 5, index - 1),		\
	SENSOR_ATTR_2(temp##index##_auto_point7_temp, S_IRUGO | S_IWUSR,\
		show_sf2_temp, store_sf2_temp, 6, index - 1)

static struct sensor_device_attribute_2 w83793_sensor_attr_2[] = {
	SENSOR_ATTR_IN(0),
	SENSOR_ATTR_IN(1),
	SENSOR_ATTR_IN(2),
	SENSOR_ATTR_IN(3),
	SENSOR_ATTR_IN(4),
	SENSOR_ATTR_IN(5),
	SENSOR_ATTR_IN(6),
	SENSOR_ATTR_IN(7),
	SENSOR_ATTR_IN(8),
	SENSOR_ATTR_IN(9),
	SENSOR_ATTR_FAN(1),
	SENSOR_ATTR_FAN(2),
	SENSOR_ATTR_FAN(3),
	SENSOR_ATTR_FAN(4),
	SENSOR_ATTR_FAN(5),
	SENSOR_ATTR_PWM(1),
	SENSOR_ATTR_PWM(2),
	SENSOR_ATTR_PWM(3),
};

static struct sensor_device_attribute_2 w83793_temp[] = {
	SENSOR_ATTR_TEMP(1),
	SENSOR_ATTR_TEMP(2),
	SENSOR_ATTR_TEMP(3),
	SENSOR_ATTR_TEMP(4),
	SENSOR_ATTR_TEMP(5),
	SENSOR_ATTR_TEMP(6),
};

/* Fan6-Fan12 */
static struct sensor_device_attribute_2 w83793_left_fan[] = {
	SENSOR_ATTR_FAN(6),
	SENSOR_ATTR_FAN(7),
	SENSOR_ATTR_FAN(8),
	SENSOR_ATTR_FAN(9),
	SENSOR_ATTR_FAN(10),
	SENSOR_ATTR_FAN(11),
	SENSOR_ATTR_FAN(12),
};

/* Pwm4-Pwm8 */
static struct sensor_device_attribute_2 w83793_left_pwm[] = {
	SENSOR_ATTR_PWM(4),
	SENSOR_ATTR_PWM(5),
	SENSOR_ATTR_PWM(6),
	SENSOR_ATTR_PWM(7),
	SENSOR_ATTR_PWM(8),
};

static struct sensor_device_attribute_2 w83793_vid[] = {
	SENSOR_ATTR_2(cpu0_vid, S_IRUGO, show_vid, NULL, NOT_USED, 0),
	SENSOR_ATTR_2(cpu1_vid, S_IRUGO, show_vid, NULL, NOT_USED, 1),
};

static struct sensor_device_attribute_2 sda_single_files[] = {
	SENSOR_ATTR_2(vrm, S_IWUSR | S_IRUGO, show_vrm, store_vrm,
		      NOT_USED, NOT_USED),
	SENSOR_ATTR_2(chassis, S_IWUSR | S_IRUGO, show_alarm_beep,
		      store_chassis_clear, ALARM_STATUS, 30),
	SENSOR_ATTR_2(beep_enable, S_IWUSR | S_IRUGO, show_beep_enable,
		      store_beep_enable, NOT_USED, NOT_USED),
	SENSOR_ATTR_2(pwm_default, S_IWUSR | S_IRUGO, show_sf_setup,
		      store_sf_setup, SETUP_PWM_DEFAULT, NOT_USED),
	SENSOR_ATTR_2(pwm_uptime, S_IWUSR | S_IRUGO, show_sf_setup,
		      store_sf_setup, SETUP_PWM_UPTIME, NOT_USED),
	SENSOR_ATTR_2(pwm_downtime, S_IWUSR | S_IRUGO, show_sf_setup,
		      store_sf_setup, SETUP_PWM_DOWNTIME, NOT_USED),
	SENSOR_ATTR_2(temp_critical, S_IWUSR | S_IRUGO, show_sf_setup,
		      store_sf_setup, SETUP_TEMP_CRITICAL, NOT_USED),
};

static void w83793_init_client(struct i2c_client *client)
{
	if (reset) {
		w83793_write_value(client, W83793_REG_CONFIG, 0x80);
	}

	/* Start monitoring */
	w83793_write_value(client, W83793_REG_CONFIG,
			   w83793_read_value(client, W83793_REG_CONFIG) | 0x01);

}

static int w83793_attach_adapter(struct i2c_adapter *adapter)
{
	if (!(adapter->class & I2C_CLASS_HWMON))
		return 0;
	return i2c_probe(adapter, &addr_data, w83793_detect);
}

static int w83793_detach_client(struct i2c_client *client)
{
	struct w83793_data *data = i2c_get_clientdata(client);
	struct device *dev = &client->dev;
	int err, i;

	/* main client */
	if (data) {
		hwmon_device_unregister(data->hwmon_dev);

		for (i = 0; i < ARRAY_SIZE(w83793_sensor_attr_2); i++)
			device_remove_file(dev,
					   &w83793_sensor_attr_2[i].dev_attr);

		for (i = 0; i < ARRAY_SIZE(sda_single_files); i++)
			device_remove_file(dev, &sda_single_files[i].dev_attr);

		for (i = 0; i < ARRAY_SIZE(w83793_vid); i++)
			device_remove_file(dev, &w83793_vid[i].dev_attr);

		for (i = 0; i < ARRAY_SIZE(w83793_left_fan); i++)
			device_remove_file(dev, &w83793_left_fan[i].dev_attr);

		for (i = 0; i < ARRAY_SIZE(w83793_left_pwm); i++)
			device_remove_file(dev, &w83793_left_pwm[i].dev_attr);

		for (i = 0; i < ARRAY_SIZE(w83793_temp); i++)
			device_remove_file(dev, &w83793_temp[i].dev_attr);
	}

	if ((err = i2c_detach_client(client)))
		return err;

	/* main client */
	if (data)
		kfree(data);
	/* subclient */
	else
		kfree(client);

	return 0;
}

static int
w83793_create_subclient(struct i2c_adapter *adapter,
			struct i2c_client *client, int addr,
			struct i2c_client **sub_cli)
{
	int err = 0;
	struct i2c_client *sub_client;

	(*sub_cli) = sub_client =
	    kzalloc(sizeof(struct i2c_client), GFP_KERNEL);
	if (!(sub_client)) {
		return -ENOMEM;
	}
	sub_client->addr = 0x48 + addr;
	i2c_set_clientdata(sub_client, NULL);
	sub_client->adapter = adapter;
	sub_client->driver = &w83793_driver;
	strlcpy(sub_client->name, "w83793 subclient", I2C_NAME_SIZE);
	if ((err = i2c_attach_client(sub_client))) {
		dev_err(&client->dev, "subclient registration "
			"at address 0x%x failed\n", sub_client->addr);
		kfree(sub_client);
	}
	return err;
}

static int
w83793_detect_subclients(struct i2c_adapter *adapter, int address,
			 int kind, struct i2c_client *client)
{
	int i, id, err;
	u8 tmp;
	struct w83793_data *data = i2c_get_clientdata(client);

	id = i2c_adapter_id(adapter);
	if (force_subclients[0] == id && force_subclients[1] == address) {
		for (i = 2; i <= 3; i++) {
			if (force_subclients[i] < 0x48
			    || force_subclients[i] > 0x4f) {
				dev_err(&client->dev,
					"invalid subclient "
					"address %d; must be 0x48-0x4f\n",
					force_subclients[i]);
				err = -EINVAL;
				goto ERROR_SC_0;
			}
		}
		w83793_write_value(client, W83793_REG_I2C_SUBADDR,
				   (force_subclients[2] & 0x07) |
				   ((force_subclients[3] & 0x07) << 4));
	}

	tmp = w83793_read_value(client, W83793_REG_I2C_SUBADDR);
	if (!(tmp & 0x08)) {
		err =
		    w83793_create_subclient(adapter, client, tmp & 0x7,
					    &data->lm75[0]);
		if (err < 0)
			goto ERROR_SC_0;
	}
	if (!(tmp & 0x80)) {
		if ((data->lm75[0] != NULL)
		    && ((tmp & 0x7) == ((tmp >> 4) & 0x7))) {
			dev_err(&client->dev,
				"duplicate addresses 0x%x, "
				"use force_subclients\n", data->lm75[0]->addr);
			err = -ENODEV;
			goto ERROR_SC_1;
		}
		err = w83793_create_subclient(adapter, client,
					      (tmp >> 4) & 0x7, &data->lm75[1]);
		if (err < 0)
			goto ERROR_SC_1;
	}

	return 0;

	/* Undo inits in case of errors */

ERROR_SC_1:
	if (data->lm75[0] != NULL) {
		i2c_detach_client(data->lm75[0]);
		kfree(data->lm75[0]);
	}
ERROR_SC_0:
	return err;
}

static int w83793_detect(struct i2c_adapter *adapter, int address, int kind)
{
	int i;
	u8 tmp, val;
	struct i2c_client *client;
	struct device *dev;
	struct w83793_data *data;
	int files_fan = ARRAY_SIZE(w83793_left_fan) / 7;
	int files_pwm = ARRAY_SIZE(w83793_left_pwm) / 5;
	int files_temp = ARRAY_SIZE(w83793_temp) / 6;
	int err = 0;

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA)) {
		goto exit;
	}

	/* OK. For now, we presume we have a valid client. We now create the
	   client structure, even though we cannot fill it completely yet.
	   But it allows us to access w83793_{read,write}_value. */

	if (!(data = kzalloc(sizeof(struct w83793_data), GFP_KERNEL))) {
		err = -ENOMEM;
		goto exit;
	}

	client = &data->client;
	dev = &client->dev;
	i2c_set_clientdata(client, data);
	client->addr = address;
	client->adapter = adapter;
	client->driver = &w83793_driver;

	data->bank = i2c_smbus_read_byte_data(client, W83793_REG_BANKSEL);

	/* Now, we do the remaining detection. */
	if (kind < 0) {
		tmp = data->bank & 0x80 ? 0x5c : 0xa3;
		/* Check Winbond vendor ID */
		if (tmp != i2c_smbus_read_byte_data(client,
							W83793_REG_VENDORID)) {
			pr_debug("w83793: Detection failed at check "
				 "vendor id\n");
			err = -ENODEV;
			goto free_mem;
		}

		/* If Winbond chip, address of chip and W83793_REG_I2C_ADDR
		   should match */
		if ((data->bank & 0x07) == 0
		 && i2c_smbus_read_byte_data(client, W83793_REG_I2C_ADDR) !=
		    (address << 1)) {
			pr_debug("w83793: Detection failed at check "
				 "i2c addr\n");
			err = -ENODEV;
			goto free_mem;
		}

	}

	/* We have either had a force parameter, or we have already detected the
	   Winbond. Determine the chip type now */

	if (kind <= 0) {
		if (0x7b == w83793_read_value(client, W83793_REG_CHIPID)) {
			kind = w83793;
		} else {
			if (kind == 0)
				dev_warn(&adapter->dev, "w83793: Ignoring "
					 "'force' parameter for unknown chip "
					 "at address 0x%02x\n", address);
			err = -ENODEV;
			goto free_mem;
		}
	}

	/* Fill in the remaining client fields and put into the global list */
	strlcpy(client->name, "w83793", I2C_NAME_SIZE);

	mutex_init(&data->update_lock);

	/* Tell the I2C layer a new client has arrived */
	if ((err = i2c_attach_client(client)))
		goto free_mem;

	if ((err = w83793_detect_subclients(adapter, address, kind, client)))
		goto detach_client;

	/* Initialize the chip */
	w83793_init_client(client);

	data->vrm = vid_which_vrm();
	/*
	   Only fan 1-5 has their own input pins,
	   Pwm 1-3 has their own pins
	 */
	data->has_fan = 0x1f;
	data->has_pwm = 0x07;
	tmp = w83793_read_value(client, W83793_REG_MFC);
	val = w83793_read_value(client, W83793_REG_FANIN_CTRL);

	/* check the function of pins 49-56 */
	if (!(tmp & 0x80)) {
		data->has_pwm |= 0x18;	/* pwm 4,5 */
		if (val & 0x01) {	/* fan 6 */
			data->has_fan |= 0x20;
			data->has_pwm |= 0x20;
		}
		if (val & 0x02) {	/* fan 7 */
			data->has_fan |= 0x40;
			data->has_pwm |= 0x40;
		}
		if (!(tmp & 0x40) && (val & 0x04)) {	/* fan 8 */
			data->has_fan |= 0x80;
			data->has_pwm |= 0x80;
		}
	}

	if (0x08 == (tmp & 0x0c)) {
		if (val & 0x08)	/* fan 9 */
			data->has_fan |= 0x100;
		if (val & 0x10)	/* fan 10 */
			data->has_fan |= 0x200;
	}

	if (0x20 == (tmp & 0x30)) {
		if (val & 0x20)	/* fan 11 */
			data->has_fan |= 0x400;
		if (val & 0x40)	/* fan 12 */
			data->has_fan |= 0x800;
	}

	if ((tmp & 0x01) && (val & 0x04)) {	/* fan 8, second location */
		data->has_fan |= 0x80;
		data->has_pwm |= 0x80;
	}

	tmp = w83793_read_value(client, W83793_REG_FANIN_SEL);
	if ((tmp & 0x01) && (val & 0x08)) {	/* fan 9, second location */
		data->has_fan |= 0x100;
	}
	if ((tmp & 0x02) && (val & 0x10)) {	/* fan 10, second location */
		data->has_fan |= 0x200;
	}
	if ((tmp & 0x04) && (val & 0x20)) {	/* fan 11, second location */
		data->has_fan |= 0x400;
	}
	if ((tmp & 0x08) && (val & 0x40)) {	/* fan 12, second location */
		data->has_fan |= 0x800;
	}

	/* check the temp1-6 mode, ignore former AMDSI selected inputs */
	tmp = w83793_read_value(client,W83793_REG_TEMP_MODE[0]);
	if (tmp & 0x01)
		data->has_temp |= 0x01;
	if (tmp & 0x04)
		data->has_temp |= 0x02;
	if (tmp & 0x10)
		data->has_temp |= 0x04;
	if (tmp & 0x40)
		data->has_temp |= 0x08;

	tmp = w83793_read_value(client,W83793_REG_TEMP_MODE[1]);
	if (tmp & 0x01)
		data->has_temp |= 0x10;
	if (tmp & 0x02)
		data->has_temp |= 0x20;

	/* Detect the VID usage and ignore unused input */
	tmp = w83793_read_value(client, W83793_REG_MFC);
	if (!(tmp & 0x29))
		data->has_vid |= 0x1;	/* has VIDA */
	if (tmp & 0x80)
		data->has_vid |= 0x2;	/* has VIDB */

	/* Register sysfs hooks */
	for (i = 0; i < ARRAY_SIZE(w83793_sensor_attr_2); i++) {
		err = device_create_file(dev,
					 &w83793_sensor_attr_2[i].dev_attr);
		if (err)
			goto exit_remove;
	}

	for (i = 0; i < ARRAY_SIZE(w83793_vid); i++) {
		if (!(data->has_vid & (1 << i)))
			continue;
		err = device_create_file(dev, &w83793_vid[i].dev_attr);
		if (err)
			goto exit_remove;
	}

	for (i = 0; i < ARRAY_SIZE(sda_single_files); i++) {
		err = device_create_file(dev, &sda_single_files[i].dev_attr);
		if (err)
			goto exit_remove;

	}

	for (i = 0; i < 6; i++) {
		int j;
		if (!(data->has_temp & (1 << i)))
			continue;
		for (j = 0; j < files_temp; j++) {
			err = device_create_file(dev,
						&w83793_temp[(i) * files_temp
								+ j].dev_attr);
			if (err)
				goto exit_remove;
		}
	}

	for (i = 5; i < 12; i++) {
		int j;
		if (!(data->has_fan & (1 << i)))
			continue;
		for (j = 0; j < files_fan; j++) {
			err = device_create_file(dev,
					   &w83793_left_fan[(i - 5) * files_fan
								+ j].dev_attr);
			if (err)
				goto exit_remove;
		}
	}

	for (i = 3; i < 8; i++) {
		int j;
		if (!(data->has_pwm & (1 << i)))
			continue;
		for (j = 0; j < files_pwm; j++) {
			err = device_create_file(dev,
					   &w83793_left_pwm[(i - 3) * files_pwm
								+ j].dev_attr);
			if (err)
				goto exit_remove;
		}
	}

	data->hwmon_dev = hwmon_device_register(dev);
	if (IS_ERR(data->hwmon_dev)) {
		err = PTR_ERR(data->hwmon_dev);
		goto exit_remove;
	}

	return 0;

	/* Unregister sysfs hooks */

exit_remove:
	for (i = 0; i < ARRAY_SIZE(w83793_sensor_attr_2); i++)
		device_remove_file(dev, &w83793_sensor_attr_2[i].dev_attr);

	for (i = 0; i < ARRAY_SIZE(sda_single_files); i++)
		device_remove_file(dev, &sda_single_files[i].dev_attr);

	for (i = 0; i < ARRAY_SIZE(w83793_vid); i++)
		device_remove_file(dev, &w83793_vid[i].dev_attr);

	for (i = 0; i < ARRAY_SIZE(w83793_left_fan); i++)
		device_remove_file(dev, &w83793_left_fan[i].dev_attr);

	for (i = 0; i < ARRAY_SIZE(w83793_left_pwm); i++)
		device_remove_file(dev, &w83793_left_pwm[i].dev_attr);

	for (i = 0; i < ARRAY_SIZE(w83793_temp); i++)
		device_remove_file(dev, &w83793_temp[i].dev_attr);

	if (data->lm75[0] != NULL) {
		i2c_detach_client(data->lm75[0]);
		kfree(data->lm75[0]);
	}
	if (data->lm75[1] != NULL) {
		i2c_detach_client(data->lm75[1]);
		kfree(data->lm75[1]);
	}
detach_client:
	i2c_detach_client(client);
free_mem:
	kfree(data);
exit:
	return err;
}

static void w83793_update_nonvolatile(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct w83793_data *data = i2c_get_clientdata(client);
	int i, j;
	/*
	   They are somewhat "stable" registers, and to update them everytime
	   takes so much time, it's just not worthy. Update them in a long
	   interval to avoid exception.
	 */
	if (!(time_after(jiffies, data->last_nonvolatile + HZ * 300)
	      || !data->valid))
		return;
	/* update voltage limits */
	for (i = 1; i < 3; i++) {
		for (j = 0; j < ARRAY_SIZE(data->in); j++) {
			data->in[j][i] =
			    w83793_read_value(client, W83793_REG_IN[j][i]);
		}
		data->in_low_bits[i] =
		    w83793_read_value(client, W83793_REG_IN_LOW_BITS[i]);
	}

	for (i = 0; i < ARRAY_SIZE(data->fan_min); i++) {
		/* Update the Fan measured value and limits */
		if (!(data->has_fan & (1 << i))) {
			continue;
		}
		data->fan_min[i] =
		    w83793_read_value(client, W83793_REG_FAN_MIN(i)) << 8;
		data->fan_min[i] |=
		    w83793_read_value(client, W83793_REG_FAN_MIN(i) + 1);
	}

	for (i = 0; i < ARRAY_SIZE(data->temp_fan_map); i++) {
		if (!(data->has_temp & (1 << i)))
			continue;
		data->temp_fan_map[i] =
		    w83793_read_value(client, W83793_REG_TEMP_FAN_MAP(i));
		for (j = 1; j < 5; j++) {
			data->temp[i][j] =
			    w83793_read_value(client, W83793_REG_TEMP[i][j]);
		}
		data->temp_cruise[i] =
		    w83793_read_value(client, W83793_REG_TEMP_CRUISE(i));
		for (j = 0; j < 7; j++) {
			data->sf2_pwm[i][j] =
			    w83793_read_value(client, W83793_REG_SF2_PWM(i, j));
			data->sf2_temp[i][j] =
			    w83793_read_value(client,
					      W83793_REG_SF2_TEMP(i, j));
		}
	}

	for (i = 0; i < ARRAY_SIZE(data->temp_mode); i++)
		data->temp_mode[i] =
		    w83793_read_value(client, W83793_REG_TEMP_MODE[i]);

	for (i = 0; i < ARRAY_SIZE(data->tolerance); i++) {
		data->tolerance[i] =
		    w83793_read_value(client, W83793_REG_TEMP_TOL(i));
	}

	for (i = 0; i < ARRAY_SIZE(data->pwm); i++) {
		if (!(data->has_pwm & (1 << i)))
			continue;
		data->pwm[i][PWM_NONSTOP] =
		    w83793_read_value(client, W83793_REG_PWM(i, PWM_NONSTOP));
		data->pwm[i][PWM_START] =
		    w83793_read_value(client, W83793_REG_PWM(i, PWM_START));
		data->pwm_stop_time[i] =
		    w83793_read_value(client, W83793_REG_PWM_STOP_TIME(i));
	}

	data->pwm_default = w83793_read_value(client, W83793_REG_PWM_DEFAULT);
	data->pwm_enable = w83793_read_value(client, W83793_REG_PWM_ENABLE);
	data->pwm_uptime = w83793_read_value(client, W83793_REG_PWM_UPTIME);
	data->pwm_downtime = w83793_read_value(client, W83793_REG_PWM_DOWNTIME);
	data->temp_critical =
	    w83793_read_value(client, W83793_REG_TEMP_CRITICAL);
	data->beep_enable = w83793_read_value(client, W83793_REG_OVT_BEEP);

	for (i = 0; i < ARRAY_SIZE(data->beeps); i++) {
		data->beeps[i] = w83793_read_value(client, W83793_REG_BEEP(i));
	}

	data->last_nonvolatile = jiffies;
}

static struct w83793_data *w83793_update_device(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct w83793_data *data = i2c_get_clientdata(client);
	int i;

	mutex_lock(&data->update_lock);

	if (!(time_after(jiffies, data->last_updated + HZ * 2)
	      || !data->valid))
		goto END;

	/* Update the voltages measured value and limits */
	for (i = 0; i < ARRAY_SIZE(data->in); i++)
		data->in[i][IN_READ] =
		    w83793_read_value(client, W83793_REG_IN[i][IN_READ]);

	data->in_low_bits[IN_READ] =
	    w83793_read_value(client, W83793_REG_IN_LOW_BITS[IN_READ]);

	for (i = 0; i < ARRAY_SIZE(data->fan); i++) {
		if (!(data->has_fan & (1 << i))) {
			continue;
		}
		data->fan[i] =
		    w83793_read_value(client, W83793_REG_FAN(i)) << 8;
		data->fan[i] |=
		    w83793_read_value(client, W83793_REG_FAN(i) + 1);
	}

	for (i = 0; i < ARRAY_SIZE(data->temp); i++) {
		if (!(data->has_temp & (1 << i)))
			continue;
		data->temp[i][TEMP_READ] =
		    w83793_read_value(client, W83793_REG_TEMP[i][TEMP_READ]);
	}

	data->temp_low_bits =
	    w83793_read_value(client, W83793_REG_TEMP_LOW_BITS);

	for (i = 0; i < ARRAY_SIZE(data->pwm); i++) {
		if (data->has_pwm & (1 << i))
			data->pwm[i][PWM_DUTY] =
			    w83793_read_value(client,
					      W83793_REG_PWM(i, PWM_DUTY));
	}

	for (i = 0; i < ARRAY_SIZE(data->alarms); i++)
		data->alarms[i] =
		    w83793_read_value(client, W83793_REG_ALARM(i));
	if (data->has_vid & 0x01)
		data->vid[0] = w83793_read_value(client, W83793_REG_VID_INA);
	if (data->has_vid & 0x02)
		data->vid[1] = w83793_read_value(client, W83793_REG_VID_INB);
	w83793_update_nonvolatile(dev);
	data->last_updated = jiffies;
	data->valid = 1;

END:
	mutex_unlock(&data->update_lock);
	return data;
}

/* Ignore the possibility that somebody change bank outside the driver
   Must be called with data->update_lock held, except during initialization */
static u8 w83793_read_value(struct i2c_client *client, u16 reg)
{
	struct w83793_data *data = i2c_get_clientdata(client);
	u8 res = 0xff;
	u8 new_bank = reg >> 8;

	new_bank |= data->bank & 0xfc;
	if (data->bank != new_bank) {
		if (i2c_smbus_write_byte_data
		    (client, W83793_REG_BANKSEL, new_bank) >= 0)
			data->bank = new_bank;
		else {
			dev_err(&client->dev,
				"set bank to %d failed, fall back "
				"to bank %d, read reg 0x%x error\n",
				new_bank, data->bank, reg);
			res = 0x0;	/* read 0x0 from the chip */
			goto END;
		}
	}
	res = i2c_smbus_read_byte_data(client, reg & 0xff);
END:
	return res;
}

/* Must be called with data->update_lock held, except during initialization */
static int w83793_write_value(struct i2c_client *client, u16 reg, u8 value)
{
	struct w83793_data *data = i2c_get_clientdata(client);
	int res;
	u8 new_bank = reg >> 8;

	new_bank |= data->bank & 0xfc;
	if (data->bank != new_bank) {
		if ((res = i2c_smbus_write_byte_data
		    (client, W83793_REG_BANKSEL, new_bank)) >= 0)
			data->bank = new_bank;
		else {
			dev_err(&client->dev,
				"set bank to %d failed, fall back "
				"to bank %d, write reg 0x%x error\n",
				new_bank, data->bank, reg);
			goto END;
		}
	}

	res = i2c_smbus_write_byte_data(client, reg & 0xff, value);
END:
	return res;
}

static int __init sensors_w83793_init(void)
{
	return i2c_add_driver(&w83793_driver);
}

static void __exit sensors_w83793_exit(void)
{
	i2c_del_driver(&w83793_driver);
}

MODULE_AUTHOR("Yuan Mu");
MODULE_DESCRIPTION("w83793 driver");
MODULE_LICENSE("GPL");

module_init(sensors_w83793_init);
module_exit(sensors_w83793_exit);
