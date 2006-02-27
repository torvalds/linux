/*
    lm85.c - Part of lm_sensors, Linux kernel modules for hardware
             monitoring
    Copyright (c) 1998, 1999  Frodo Looijaard <frodol@dds.nl> 
    Copyright (c) 2002, 2003  Philip Pokorny <ppokorny@penguincomputing.com>
    Copyright (c) 2003        Margit Schubert-While <margitsw@t-online.de>
    Copyright (c) 2004        Justin Thiessen <jthiessen@penguincomputing.com>

    Chip details at	      <http://www.national.com/ds/LM/LM85.pdf>

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
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/jiffies.h>
#include <linux/i2c.h>
#include <linux/hwmon.h>
#include <linux/hwmon-vid.h>
#include <linux/err.h>
#include <linux/mutex.h>

/* Addresses to scan */
static unsigned short normal_i2c[] = { 0x2c, 0x2d, 0x2e, I2C_CLIENT_END };

/* Insmod parameters */
I2C_CLIENT_INSMOD_6(lm85b, lm85c, adm1027, adt7463, emc6d100, emc6d102);

/* The LM85 registers */

#define	LM85_REG_IN(nr)			(0x20 + (nr))
#define	LM85_REG_IN_MIN(nr)		(0x44 + (nr) * 2)
#define	LM85_REG_IN_MAX(nr)		(0x45 + (nr) * 2)

#define	LM85_REG_TEMP(nr)		(0x25 + (nr))
#define	LM85_REG_TEMP_MIN(nr)		(0x4e + (nr) * 2)
#define	LM85_REG_TEMP_MAX(nr)		(0x4f + (nr) * 2)

/* Fan speeds are LSB, MSB (2 bytes) */
#define	LM85_REG_FAN(nr)		(0x28 + (nr) *2)
#define	LM85_REG_FAN_MIN(nr)		(0x54 + (nr) *2)

#define	LM85_REG_PWM(nr)		(0x30 + (nr))

#define	ADT7463_REG_OPPOINT(nr)		(0x33 + (nr))

#define	ADT7463_REG_TMIN_CTL1		0x36
#define	ADT7463_REG_TMIN_CTL2		0x37

#define	LM85_REG_DEVICE			0x3d
#define	LM85_REG_COMPANY		0x3e
#define	LM85_REG_VERSTEP		0x3f
/* These are the recognized values for the above regs */
#define	LM85_DEVICE_ADX			0x27
#define	LM85_COMPANY_NATIONAL		0x01
#define	LM85_COMPANY_ANALOG_DEV		0x41
#define	LM85_COMPANY_SMSC      		0x5c
#define	LM85_VERSTEP_VMASK              0xf0
#define	LM85_VERSTEP_GENERIC		0x60
#define	LM85_VERSTEP_LM85C		0x60
#define	LM85_VERSTEP_LM85B		0x62
#define	LM85_VERSTEP_ADM1027		0x60
#define	LM85_VERSTEP_ADT7463		0x62
#define	LM85_VERSTEP_ADT7463C		0x6A
#define	LM85_VERSTEP_EMC6D100_A0        0x60
#define	LM85_VERSTEP_EMC6D100_A1        0x61
#define	LM85_VERSTEP_EMC6D102		0x65

#define	LM85_REG_CONFIG			0x40

#define	LM85_REG_ALARM1			0x41
#define	LM85_REG_ALARM2			0x42

#define	LM85_REG_VID			0x43

/* Automated FAN control */
#define	LM85_REG_AFAN_CONFIG(nr)	(0x5c + (nr))
#define	LM85_REG_AFAN_RANGE(nr)		(0x5f + (nr))
#define	LM85_REG_AFAN_SPIKE1		0x62
#define	LM85_REG_AFAN_SPIKE2		0x63
#define	LM85_REG_AFAN_MINPWM(nr)	(0x64 + (nr))
#define	LM85_REG_AFAN_LIMIT(nr)		(0x67 + (nr))
#define	LM85_REG_AFAN_CRITICAL(nr)	(0x6a + (nr))
#define	LM85_REG_AFAN_HYST1		0x6d
#define	LM85_REG_AFAN_HYST2		0x6e

#define	LM85_REG_TACH_MODE		0x74
#define	LM85_REG_SPINUP_CTL		0x75

#define	ADM1027_REG_TEMP_OFFSET(nr)	(0x70 + (nr))
#define	ADM1027_REG_CONFIG2		0x73
#define	ADM1027_REG_INTMASK1		0x74
#define	ADM1027_REG_INTMASK2		0x75
#define	ADM1027_REG_EXTEND_ADC1		0x76
#define	ADM1027_REG_EXTEND_ADC2		0x77
#define	ADM1027_REG_CONFIG3		0x78
#define	ADM1027_REG_FAN_PPR		0x7b

#define	ADT7463_REG_THERM		0x79
#define	ADT7463_REG_THERM_LIMIT		0x7A

#define EMC6D100_REG_ALARM3             0x7d
/* IN5, IN6 and IN7 */
#define	EMC6D100_REG_IN(nr)             (0x70 + ((nr)-5))
#define	EMC6D100_REG_IN_MIN(nr)         (0x73 + ((nr)-5) * 2)
#define	EMC6D100_REG_IN_MAX(nr)         (0x74 + ((nr)-5) * 2)
#define	EMC6D102_REG_EXTEND_ADC1	0x85
#define	EMC6D102_REG_EXTEND_ADC2	0x86
#define	EMC6D102_REG_EXTEND_ADC3	0x87
#define	EMC6D102_REG_EXTEND_ADC4	0x88

#define	LM85_ALARM_IN0			0x0001
#define	LM85_ALARM_IN1			0x0002
#define	LM85_ALARM_IN2			0x0004
#define	LM85_ALARM_IN3			0x0008
#define	LM85_ALARM_TEMP1		0x0010
#define	LM85_ALARM_TEMP2		0x0020
#define	LM85_ALARM_TEMP3		0x0040
#define	LM85_ALARM_ALARM2		0x0080
#define	LM85_ALARM_IN4			0x0100
#define	LM85_ALARM_RESERVED		0x0200
#define	LM85_ALARM_FAN1			0x0400
#define	LM85_ALARM_FAN2			0x0800
#define	LM85_ALARM_FAN3			0x1000
#define	LM85_ALARM_FAN4			0x2000
#define	LM85_ALARM_TEMP1_FAULT		0x4000
#define	LM85_ALARM_TEMP3_FAULT		0x8000


/* Conversions. Rounding and limit checking is only done on the TO_REG 
   variants. Note that you should be a bit careful with which arguments
   these macros are called: arguments may be evaluated more than once.
 */

/* IN are scaled acording to built-in resistors */
static int lm85_scaling[] = {  /* .001 Volts */
		2500, 2250, 3300, 5000, 12000,
		3300, 1500, 1800 /*EMC6D100*/
	};
#define SCALE(val,from,to)		(((val)*(to) + ((from)/2))/(from))

#define INS_TO_REG(n,val)	\
		SENSORS_LIMIT(SCALE(val,lm85_scaling[n],192),0,255)

#define INSEXT_FROM_REG(n,val,ext,scale)	\
		SCALE((val)*(scale) + (ext),192*(scale),lm85_scaling[n])

#define INS_FROM_REG(n,val)   INSEXT_FROM_REG(n,val,0,1)

/* FAN speed is measured using 90kHz clock */
#define FAN_TO_REG(val)		(SENSORS_LIMIT( (val)<=0?0: 5400000/(val),0,65534))
#define FAN_FROM_REG(val)	((val)==0?-1:(val)==0xffff?0:5400000/(val))

/* Temperature is reported in .001 degC increments */
#define TEMP_TO_REG(val)	\
		SENSORS_LIMIT(SCALE(val,1000,1),-127,127)
#define TEMPEXT_FROM_REG(val,ext,scale)	\
		SCALE((val)*scale + (ext),scale,1000)
#define TEMP_FROM_REG(val)	\
		TEMPEXT_FROM_REG(val,0,1)

#define PWM_TO_REG(val)			(SENSORS_LIMIT(val,0,255))
#define PWM_FROM_REG(val)		(val)


/* ZONEs have the following parameters:
 *    Limit (low) temp,           1. degC
 *    Hysteresis (below limit),   1. degC (0-15)
 *    Range of speed control,     .1 degC (2-80)
 *    Critical (high) temp,       1. degC
 *
 * FAN PWMs have the following parameters:
 *    Reference Zone,                 1, 2, 3, etc.
 *    Spinup time,                    .05 sec
 *    PWM value at limit/low temp,    1 count
 *    PWM Frequency,                  1. Hz
 *    PWM is Min or OFF below limit,  flag
 *    Invert PWM output,              flag
 *
 * Some chips filter the temp, others the fan.
 *    Filter constant (or disabled)   .1 seconds
 */

/* These are the zone temperature range encodings in .001 degree C */
static int lm85_range_map[] = {   
		2000,  2500,  3300,  4000,  5000,  6600,
		8000, 10000, 13300, 16000, 20000, 26600,
		32000, 40000, 53300, 80000
	};
static int RANGE_TO_REG( int range )
{
	int i;

	if ( range < lm85_range_map[0] ) { 
		return 0 ;
	} else if ( range > lm85_range_map[15] ) {
		return 15 ;
	} else {  /* find closest match */
		for ( i = 14 ; i >= 0 ; --i ) {
			if ( range > lm85_range_map[i] ) { /* range bracketed */
				if ((lm85_range_map[i+1] - range) < 
					(range - lm85_range_map[i])) {
					i++;
					break;
				}
				break;
			}
		}
	}
	return( i & 0x0f );
}
#define RANGE_FROM_REG(val) (lm85_range_map[(val)&0x0f])

/* These are the Acoustic Enhancement, or Temperature smoothing encodings
 * NOTE: The enable/disable bit is INCLUDED in these encodings as the
 *       MSB (bit 3, value 8).  If the enable bit is 0, the encoded value
 *       is ignored, or set to 0.
 */
/* These are the PWM frequency encodings */
static int lm85_freq_map[] = { /* .1 Hz */
		100, 150, 230, 300, 380, 470, 620, 940
	};
static int FREQ_TO_REG( int freq )
{
	int i;

	if( freq >= lm85_freq_map[7] ) { return 7 ; }
	for( i = 0 ; i < 7 ; ++i )
		if( freq <= lm85_freq_map[i] )
			break ;
	return( i & 0x07 );
}
#define FREQ_FROM_REG(val) (lm85_freq_map[(val)&0x07])

/* Since we can't use strings, I'm abusing these numbers
 *   to stand in for the following meanings:
 *      1 -- PWM responds to Zone 1
 *      2 -- PWM responds to Zone 2
 *      3 -- PWM responds to Zone 3
 *     23 -- PWM responds to the higher temp of Zone 2 or 3
 *    123 -- PWM responds to highest of Zone 1, 2, or 3
 *      0 -- PWM is always at 0% (ie, off)
 *     -1 -- PWM is always at 100%
 *     -2 -- PWM responds to manual control
 */

static int lm85_zone_map[] = { 1, 2, 3, -1, 0, 23, 123, -2 };
#define ZONE_FROM_REG(val) (lm85_zone_map[((val)>>5)&0x07])

static int ZONE_TO_REG( int zone )
{
	int i;

	for( i = 0 ; i <= 7 ; ++i )
		if( zone == lm85_zone_map[i] )
			break ;
	if( i > 7 )   /* Not found. */
		i = 3;  /* Always 100% */
	return( (i & 0x07)<<5 );
}

#define HYST_TO_REG(val) (SENSORS_LIMIT(((val)+500)/1000,0,15))
#define HYST_FROM_REG(val) ((val)*1000)

#define OFFSET_TO_REG(val) (SENSORS_LIMIT((val)/25,-127,127))
#define OFFSET_FROM_REG(val) ((val)*25)

#define PPR_MASK(fan) (0x03<<(fan *2))
#define PPR_TO_REG(val,fan) (SENSORS_LIMIT((val)-1,0,3)<<(fan *2))
#define PPR_FROM_REG(val,fan) ((((val)>>(fan * 2))&0x03)+1)

/* Chip sampling rates
 *
 * Some sensors are not updated more frequently than once per second
 *    so it doesn't make sense to read them more often than that.
 *    We cache the results and return the saved data if the driver
 *    is called again before a second has elapsed.
 *
 * Also, there is significant configuration data for this chip
 *    given the automatic PWM fan control that is possible.  There
 *    are about 47 bytes of config data to only 22 bytes of actual
 *    readings.  So, we keep the config data up to date in the cache
 *    when it is written and only sample it once every 1 *minute*
 */
#define LM85_DATA_INTERVAL  (HZ + HZ / 2)
#define LM85_CONFIG_INTERVAL  (1 * 60 * HZ)

/* For each registered LM85, we need to keep some data in memory. That
   data is pointed to by lm85_list[NR]->data. The structure itself is
   dynamically allocated, at the same time when a new lm85 client is
   allocated. */

/* LM85 can automatically adjust fan speeds based on temperature
 * This structure encapsulates an entire Zone config.  There are
 * three zones (one for each temperature input) on the lm85
 */
struct lm85_zone {
	s8 limit;	/* Low temp limit */
	u8 hyst;	/* Low limit hysteresis. (0-15) */
	u8 range;	/* Temp range, encoded */
	s8 critical;	/* "All fans ON" temp limit */
	u8 off_desired; /* Actual "off" temperature specified.  Preserved 
			 * to prevent "drift" as other autofan control
			 * values change.
			 */
	u8 max_desired; /* Actual "max" temperature specified.  Preserved 
			 * to prevent "drift" as other autofan control
			 * values change.
			 */
};

struct lm85_autofan {
	u8 config;	/* Register value */
	u8 freq;	/* PWM frequency, encoded */
	u8 min_pwm;	/* Minimum PWM value, encoded */
	u8 min_off;	/* Min PWM or OFF below "limit", flag */
};

struct lm85_data {
	struct i2c_client client;
	struct class_device *class_dev;
	struct mutex lock;
	enum chips type;

	struct mutex update_lock;
	int valid;		/* !=0 if following fields are valid */
	unsigned long last_reading;	/* In jiffies */
	unsigned long last_config;	/* In jiffies */

	u8 in[8];		/* Register value */
	u8 in_max[8];		/* Register value */
	u8 in_min[8];		/* Register value */
	s8 temp[3];		/* Register value */
	s8 temp_min[3];		/* Register value */
	s8 temp_max[3];		/* Register value */
	s8 temp_offset[3];	/* Register value */
	u16 fan[4];		/* Register value */
	u16 fan_min[4];		/* Register value */
	u8 pwm[3];		/* Register value */
	u8 spinup_ctl;		/* Register encoding, combined */
	u8 tach_mode;		/* Register encoding, combined */
	u8 temp_ext[3];		/* Decoded values */
	u8 in_ext[8];		/* Decoded values */
	u8 adc_scale;		/* ADC Extended bits scaling factor */
	u8 fan_ppr;		/* Register value */
	u8 smooth[3];		/* Register encoding */
	u8 vid;			/* Register value */
	u8 vrm;			/* VRM version */
	u8 syncpwm3;		/* Saved PWM3 for TACH 2,3,4 config */
	u8 oppoint[3];		/* Register value */
	u16 tmin_ctl;		/* Register value */
	unsigned long therm_total; /* Cummulative therm count */
	u8 therm_limit;		/* Register value */
	u32 alarms;		/* Register encoding, combined */
	struct lm85_autofan autofan[3];
	struct lm85_zone zone[3];
};

static int lm85_attach_adapter(struct i2c_adapter *adapter);
static int lm85_detect(struct i2c_adapter *adapter, int address,
			int kind);
static int lm85_detach_client(struct i2c_client *client);

static int lm85_read_value(struct i2c_client *client, u8 reg);
static int lm85_write_value(struct i2c_client *client, u8 reg, int value);
static struct lm85_data *lm85_update_device(struct device *dev);
static void lm85_init_client(struct i2c_client *client);


static struct i2c_driver lm85_driver = {
	.driver = {
		.name   = "lm85",
	},
	.id             = I2C_DRIVERID_LM85,
	.attach_adapter = lm85_attach_adapter,
	.detach_client  = lm85_detach_client,
};


/* 4 Fans */
static ssize_t show_fan(struct device *dev, char *buf, int nr)
{
	struct lm85_data *data = lm85_update_device(dev);
	return sprintf(buf,"%d\n", FAN_FROM_REG(data->fan[nr]) );
}
static ssize_t show_fan_min(struct device *dev, char *buf, int nr)
{
	struct lm85_data *data = lm85_update_device(dev);
	return sprintf(buf,"%d\n", FAN_FROM_REG(data->fan_min[nr]) );
}
static ssize_t set_fan_min(struct device *dev, const char *buf, 
		size_t count, int nr)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct lm85_data *data = i2c_get_clientdata(client);
	long val = simple_strtol(buf, NULL, 10);

	mutex_lock(&data->update_lock);
	data->fan_min[nr] = FAN_TO_REG(val);
	lm85_write_value(client, LM85_REG_FAN_MIN(nr), data->fan_min[nr]);
	mutex_unlock(&data->update_lock);
	return count;
}

#define show_fan_offset(offset)						\
static ssize_t show_fan_##offset (struct device *dev, struct device_attribute *attr, char *buf)	\
{									\
	return show_fan(dev, buf, offset - 1);				\
}									\
static ssize_t show_fan_##offset##_min (struct device *dev, struct device_attribute *attr, char *buf)	\
{									\
	return show_fan_min(dev, buf, offset - 1);			\
}									\
static ssize_t set_fan_##offset##_min (struct device *dev, struct device_attribute *attr, 		\
	const char *buf, size_t count) 					\
{									\
	return set_fan_min(dev, buf, count, offset - 1);		\
}									\
static DEVICE_ATTR(fan##offset##_input, S_IRUGO, show_fan_##offset,	\
		NULL);							\
static DEVICE_ATTR(fan##offset##_min, S_IRUGO | S_IWUSR, 		\
		show_fan_##offset##_min, set_fan_##offset##_min);

show_fan_offset(1);
show_fan_offset(2);
show_fan_offset(3);
show_fan_offset(4);

/* vid, vrm, alarms */

static ssize_t show_vid_reg(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct lm85_data *data = lm85_update_device(dev);
	int vid;

	if (data->type == adt7463 && (data->vid & 0x80)) {
		/* 6-pin VID (VRM 10) */
		vid = vid_from_reg(data->vid & 0x3f, data->vrm);
	} else {
		/* 5-pin VID (VRM 9) */
		vid = vid_from_reg(data->vid & 0x1f, data->vrm);
	}

	return sprintf(buf, "%d\n", vid);
}

static DEVICE_ATTR(cpu0_vid, S_IRUGO, show_vid_reg, NULL);

static ssize_t show_vrm_reg(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct lm85_data *data = lm85_update_device(dev);
	return sprintf(buf, "%ld\n", (long) data->vrm);
}

static ssize_t store_vrm_reg(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct lm85_data *data = i2c_get_clientdata(client);
	u32 val;

	val = simple_strtoul(buf, NULL, 10);
	data->vrm = val;
	return count;
}

static DEVICE_ATTR(vrm, S_IRUGO | S_IWUSR, show_vrm_reg, store_vrm_reg);

static ssize_t show_alarms_reg(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct lm85_data *data = lm85_update_device(dev);
	return sprintf(buf, "%u\n", data->alarms);
}

static DEVICE_ATTR(alarms, S_IRUGO, show_alarms_reg, NULL);

/* pwm */

static ssize_t show_pwm(struct device *dev, char *buf, int nr)
{
	struct lm85_data *data = lm85_update_device(dev);
	return sprintf(buf,"%d\n", PWM_FROM_REG(data->pwm[nr]) );
}
static ssize_t set_pwm(struct device *dev, const char *buf, 
		size_t count, int nr)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct lm85_data *data = i2c_get_clientdata(client);
	long val = simple_strtol(buf, NULL, 10);

	mutex_lock(&data->update_lock);
	data->pwm[nr] = PWM_TO_REG(val);
	lm85_write_value(client, LM85_REG_PWM(nr), data->pwm[nr]);
	mutex_unlock(&data->update_lock);
	return count;
}
static ssize_t show_pwm_enable(struct device *dev, char *buf, int nr)
{
	struct lm85_data *data = lm85_update_device(dev);
	int	pwm_zone;

	pwm_zone = ZONE_FROM_REG(data->autofan[nr].config);
	return sprintf(buf,"%d\n", (pwm_zone != 0 && pwm_zone != -1) );
}

#define show_pwm_reg(offset)						\
static ssize_t show_pwm_##offset (struct device *dev, struct device_attribute *attr, char *buf)	\
{									\
	return show_pwm(dev, buf, offset - 1);				\
}									\
static ssize_t set_pwm_##offset (struct device *dev, struct device_attribute *attr,			\
				 const char *buf, size_t count)		\
{									\
	return set_pwm(dev, buf, count, offset - 1);			\
}									\
static ssize_t show_pwm_enable##offset (struct device *dev, struct device_attribute *attr, char *buf)	\
{									\
	return show_pwm_enable(dev, buf, offset - 1);			\
}									\
static DEVICE_ATTR(pwm##offset, S_IRUGO | S_IWUSR, 			\
		show_pwm_##offset, set_pwm_##offset);			\
static DEVICE_ATTR(pwm##offset##_enable, S_IRUGO, 			\
		show_pwm_enable##offset, NULL);

show_pwm_reg(1);
show_pwm_reg(2);
show_pwm_reg(3);

/* Voltages */

static ssize_t show_in(struct device *dev, char *buf, int nr)
{
	struct lm85_data *data = lm85_update_device(dev);
	return sprintf(	buf, "%d\n", INSEXT_FROM_REG(nr,
						     data->in[nr],
						     data->in_ext[nr],
						     data->adc_scale) );
}
static ssize_t show_in_min(struct device *dev, char *buf, int nr)
{
	struct lm85_data *data = lm85_update_device(dev);
	return sprintf(buf,"%d\n", INS_FROM_REG(nr, data->in_min[nr]) );
}
static ssize_t set_in_min(struct device *dev, const char *buf, 
		size_t count, int nr)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct lm85_data *data = i2c_get_clientdata(client);
	long val = simple_strtol(buf, NULL, 10);

	mutex_lock(&data->update_lock);
	data->in_min[nr] = INS_TO_REG(nr, val);
	lm85_write_value(client, LM85_REG_IN_MIN(nr), data->in_min[nr]);
	mutex_unlock(&data->update_lock);
	return count;
}
static ssize_t show_in_max(struct device *dev, char *buf, int nr)
{
	struct lm85_data *data = lm85_update_device(dev);
	return sprintf(buf,"%d\n", INS_FROM_REG(nr, data->in_max[nr]) );
}
static ssize_t set_in_max(struct device *dev, const char *buf, 
		size_t count, int nr)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct lm85_data *data = i2c_get_clientdata(client);
	long val = simple_strtol(buf, NULL, 10);

	mutex_lock(&data->update_lock);
	data->in_max[nr] = INS_TO_REG(nr, val);
	lm85_write_value(client, LM85_REG_IN_MAX(nr), data->in_max[nr]);
	mutex_unlock(&data->update_lock);
	return count;
}
#define show_in_reg(offset)						\
static ssize_t show_in_##offset (struct device *dev, struct device_attribute *attr, char *buf)		\
{									\
	return show_in(dev, buf, offset);				\
}									\
static ssize_t show_in_##offset##_min (struct device *dev, struct device_attribute *attr, char *buf)	\
{									\
	return show_in_min(dev, buf, offset);				\
}									\
static ssize_t show_in_##offset##_max (struct device *dev, struct device_attribute *attr, char *buf)	\
{									\
	return show_in_max(dev, buf, offset);				\
}									\
static ssize_t set_in_##offset##_min (struct device *dev, struct device_attribute *attr, 		\
	const char *buf, size_t count) 					\
{									\
	return set_in_min(dev, buf, count, offset);			\
}									\
static ssize_t set_in_##offset##_max (struct device *dev, struct device_attribute *attr, 		\
	const char *buf, size_t count) 					\
{									\
	return set_in_max(dev, buf, count, offset);			\
}									\
static DEVICE_ATTR(in##offset##_input, S_IRUGO, show_in_##offset, 	\
		NULL);							\
static DEVICE_ATTR(in##offset##_min, S_IRUGO | S_IWUSR, 		\
		show_in_##offset##_min, set_in_##offset##_min);		\
static DEVICE_ATTR(in##offset##_max, S_IRUGO | S_IWUSR, 		\
		show_in_##offset##_max, set_in_##offset##_max);

show_in_reg(0);
show_in_reg(1);
show_in_reg(2);
show_in_reg(3);
show_in_reg(4);

/* Temps */

static ssize_t show_temp(struct device *dev, char *buf, int nr)
{
	struct lm85_data *data = lm85_update_device(dev);
	return sprintf(buf,"%d\n", TEMPEXT_FROM_REG(data->temp[nr],
						    data->temp_ext[nr],
						    data->adc_scale) );
}
static ssize_t show_temp_min(struct device *dev, char *buf, int nr)
{
	struct lm85_data *data = lm85_update_device(dev);
	return sprintf(buf,"%d\n", TEMP_FROM_REG(data->temp_min[nr]) );
}
static ssize_t set_temp_min(struct device *dev, const char *buf, 
		size_t count, int nr)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct lm85_data *data = i2c_get_clientdata(client);
	long val = simple_strtol(buf, NULL, 10);

	mutex_lock(&data->update_lock);
	data->temp_min[nr] = TEMP_TO_REG(val);
	lm85_write_value(client, LM85_REG_TEMP_MIN(nr), data->temp_min[nr]);
	mutex_unlock(&data->update_lock);
	return count;
}
static ssize_t show_temp_max(struct device *dev, char *buf, int nr)
{
	struct lm85_data *data = lm85_update_device(dev);
	return sprintf(buf,"%d\n", TEMP_FROM_REG(data->temp_max[nr]) );
}
static ssize_t set_temp_max(struct device *dev, const char *buf, 
		size_t count, int nr)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct lm85_data *data = i2c_get_clientdata(client);
	long val = simple_strtol(buf, NULL, 10);	

	mutex_lock(&data->update_lock);
	data->temp_max[nr] = TEMP_TO_REG(val);
	lm85_write_value(client, LM85_REG_TEMP_MAX(nr), data->temp_max[nr]);
	mutex_unlock(&data->update_lock);
	return count;
}
#define show_temp_reg(offset)						\
static ssize_t show_temp_##offset (struct device *dev, struct device_attribute *attr, char *buf)	\
{									\
	return show_temp(dev, buf, offset - 1);				\
}									\
static ssize_t show_temp_##offset##_min (struct device *dev, struct device_attribute *attr, char *buf)	\
{									\
	return show_temp_min(dev, buf, offset - 1);			\
}									\
static ssize_t show_temp_##offset##_max (struct device *dev, struct device_attribute *attr, char *buf)	\
{									\
	return show_temp_max(dev, buf, offset - 1);			\
}									\
static ssize_t set_temp_##offset##_min (struct device *dev, struct device_attribute *attr, 		\
	const char *buf, size_t count) 					\
{									\
	return set_temp_min(dev, buf, count, offset - 1);		\
}									\
static ssize_t set_temp_##offset##_max (struct device *dev, struct device_attribute *attr, 		\
	const char *buf, size_t count) 					\
{									\
	return set_temp_max(dev, buf, count, offset - 1);		\
}									\
static DEVICE_ATTR(temp##offset##_input, S_IRUGO, show_temp_##offset,	\
		NULL);							\
static DEVICE_ATTR(temp##offset##_min, S_IRUGO | S_IWUSR, 		\
		show_temp_##offset##_min, set_temp_##offset##_min);	\
static DEVICE_ATTR(temp##offset##_max, S_IRUGO | S_IWUSR, 		\
		show_temp_##offset##_max, set_temp_##offset##_max);

show_temp_reg(1);
show_temp_reg(2);
show_temp_reg(3);


/* Automatic PWM control */

static ssize_t show_pwm_auto_channels(struct device *dev, char *buf, int nr)
{
	struct lm85_data *data = lm85_update_device(dev);
	return sprintf(buf,"%d\n", ZONE_FROM_REG(data->autofan[nr].config));
}
static ssize_t set_pwm_auto_channels(struct device *dev, const char *buf,
	size_t count, int nr)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct lm85_data *data = i2c_get_clientdata(client);
	long val = simple_strtol(buf, NULL, 10);   

	mutex_lock(&data->update_lock);
	data->autofan[nr].config = (data->autofan[nr].config & (~0xe0))
		| ZONE_TO_REG(val) ;
	lm85_write_value(client, LM85_REG_AFAN_CONFIG(nr),
		data->autofan[nr].config);
	mutex_unlock(&data->update_lock);
	return count;
}
static ssize_t show_pwm_auto_pwm_min(struct device *dev, char *buf, int nr)
{
	struct lm85_data *data = lm85_update_device(dev);
	return sprintf(buf,"%d\n", PWM_FROM_REG(data->autofan[nr].min_pwm));
}
static ssize_t set_pwm_auto_pwm_min(struct device *dev, const char *buf,
	size_t count, int nr)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct lm85_data *data = i2c_get_clientdata(client);
	long val = simple_strtol(buf, NULL, 10);

	mutex_lock(&data->update_lock);
	data->autofan[nr].min_pwm = PWM_TO_REG(val);
	lm85_write_value(client, LM85_REG_AFAN_MINPWM(nr),
		data->autofan[nr].min_pwm);
	mutex_unlock(&data->update_lock);
	return count;
}
static ssize_t show_pwm_auto_pwm_minctl(struct device *dev, char *buf, int nr)
{
	struct lm85_data *data = lm85_update_device(dev);
	return sprintf(buf,"%d\n", data->autofan[nr].min_off);
}
static ssize_t set_pwm_auto_pwm_minctl(struct device *dev, const char *buf,
	size_t count, int nr)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct lm85_data *data = i2c_get_clientdata(client);
	long val = simple_strtol(buf, NULL, 10);

	mutex_lock(&data->update_lock);
	data->autofan[nr].min_off = val;
	lm85_write_value(client, LM85_REG_AFAN_SPIKE1, data->smooth[0]
		| data->syncpwm3
		| (data->autofan[0].min_off ? 0x20 : 0)
		| (data->autofan[1].min_off ? 0x40 : 0)
		| (data->autofan[2].min_off ? 0x80 : 0)
	);
	mutex_unlock(&data->update_lock);
	return count;
}
static ssize_t show_pwm_auto_pwm_freq(struct device *dev, char *buf, int nr)
{
	struct lm85_data *data = lm85_update_device(dev);
	return sprintf(buf,"%d\n", FREQ_FROM_REG(data->autofan[nr].freq));
}
static ssize_t set_pwm_auto_pwm_freq(struct device *dev, const char *buf,
		size_t count, int nr)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct lm85_data *data = i2c_get_clientdata(client);
	long val = simple_strtol(buf, NULL, 10);

	mutex_lock(&data->update_lock);
	data->autofan[nr].freq = FREQ_TO_REG(val);
	lm85_write_value(client, LM85_REG_AFAN_RANGE(nr),
		(data->zone[nr].range << 4)
		| data->autofan[nr].freq
	); 
	mutex_unlock(&data->update_lock);
	return count;
}
#define pwm_auto(offset)						\
static ssize_t show_pwm##offset##_auto_channels (struct device *dev, struct device_attribute *attr,	\
	char *buf)							\
{									\
	return show_pwm_auto_channels(dev, buf, offset - 1);		\
}									\
static ssize_t set_pwm##offset##_auto_channels (struct device *dev, struct device_attribute *attr,	\
	const char *buf, size_t count)					\
{									\
	return set_pwm_auto_channels(dev, buf, count, offset - 1);	\
}									\
static ssize_t show_pwm##offset##_auto_pwm_min (struct device *dev, struct device_attribute *attr,	\
	char *buf)							\
{									\
	return show_pwm_auto_pwm_min(dev, buf, offset - 1);		\
}									\
static ssize_t set_pwm##offset##_auto_pwm_min (struct device *dev, struct device_attribute *attr,	\
	const char *buf, size_t count)					\
{									\
	return set_pwm_auto_pwm_min(dev, buf, count, offset - 1);	\
}									\
static ssize_t show_pwm##offset##_auto_pwm_minctl (struct device *dev, struct device_attribute *attr,	\
	char *buf)							\
{									\
	return show_pwm_auto_pwm_minctl(dev, buf, offset - 1);		\
}									\
static ssize_t set_pwm##offset##_auto_pwm_minctl (struct device *dev, struct device_attribute *attr,	\
	const char *buf, size_t count)					\
{									\
	return set_pwm_auto_pwm_minctl(dev, buf, count, offset - 1);	\
}									\
static ssize_t show_pwm##offset##_auto_pwm_freq (struct device *dev, struct device_attribute *attr,	\
	char *buf)							\
{									\
	return show_pwm_auto_pwm_freq(dev, buf, offset - 1);		\
}									\
static ssize_t set_pwm##offset##_auto_pwm_freq(struct device *dev, struct device_attribute *attr,	\
	const char *buf, size_t count)					\
{									\
	return set_pwm_auto_pwm_freq(dev, buf, count, offset - 1);	\
}									\
static DEVICE_ATTR(pwm##offset##_auto_channels, S_IRUGO | S_IWUSR,	\
		show_pwm##offset##_auto_channels,			\
		set_pwm##offset##_auto_channels);			\
static DEVICE_ATTR(pwm##offset##_auto_pwm_min, S_IRUGO | S_IWUSR,	\
		show_pwm##offset##_auto_pwm_min,			\
		set_pwm##offset##_auto_pwm_min);			\
static DEVICE_ATTR(pwm##offset##_auto_pwm_minctl, S_IRUGO | S_IWUSR,	\
		show_pwm##offset##_auto_pwm_minctl,			\
		set_pwm##offset##_auto_pwm_minctl);			\
static DEVICE_ATTR(pwm##offset##_auto_pwm_freq, S_IRUGO | S_IWUSR,	\
		show_pwm##offset##_auto_pwm_freq,			\
		set_pwm##offset##_auto_pwm_freq);              
pwm_auto(1);
pwm_auto(2);
pwm_auto(3);

/* Temperature settings for automatic PWM control */

static ssize_t show_temp_auto_temp_off(struct device *dev, char *buf, int nr)
{
	struct lm85_data *data = lm85_update_device(dev);
	return sprintf(buf,"%d\n", TEMP_FROM_REG(data->zone[nr].limit) -
		HYST_FROM_REG(data->zone[nr].hyst));
}
static ssize_t set_temp_auto_temp_off(struct device *dev, const char *buf,
	size_t count, int nr)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct lm85_data *data = i2c_get_clientdata(client);
	int min;
	long val = simple_strtol(buf, NULL, 10);

	mutex_lock(&data->update_lock);
	min = TEMP_FROM_REG(data->zone[nr].limit);
	data->zone[nr].off_desired = TEMP_TO_REG(val);
	data->zone[nr].hyst = HYST_TO_REG(min - val);
	if ( nr == 0 || nr == 1 ) {
		lm85_write_value(client, LM85_REG_AFAN_HYST1,
			(data->zone[0].hyst << 4)
			| data->zone[1].hyst
			);
	} else {
		lm85_write_value(client, LM85_REG_AFAN_HYST2,
			(data->zone[2].hyst << 4)
		);
	}
	mutex_unlock(&data->update_lock);
	return count;
}
static ssize_t show_temp_auto_temp_min(struct device *dev, char *buf, int nr)
{
	struct lm85_data *data = lm85_update_device(dev);
	return sprintf(buf,"%d\n", TEMP_FROM_REG(data->zone[nr].limit) );
}
static ssize_t set_temp_auto_temp_min(struct device *dev, const char *buf,
	size_t count, int nr)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct lm85_data *data = i2c_get_clientdata(client);
	long val = simple_strtol(buf, NULL, 10);

	mutex_lock(&data->update_lock);
	data->zone[nr].limit = TEMP_TO_REG(val);
	lm85_write_value(client, LM85_REG_AFAN_LIMIT(nr),
		data->zone[nr].limit);

/* Update temp_auto_max and temp_auto_range */
	data->zone[nr].range = RANGE_TO_REG(
		TEMP_FROM_REG(data->zone[nr].max_desired) -
		TEMP_FROM_REG(data->zone[nr].limit));
	lm85_write_value(client, LM85_REG_AFAN_RANGE(nr),
		((data->zone[nr].range & 0x0f) << 4)
		| (data->autofan[nr].freq & 0x07));

/* Update temp_auto_hyst and temp_auto_off */
	data->zone[nr].hyst = HYST_TO_REG(TEMP_FROM_REG(
		data->zone[nr].limit) - TEMP_FROM_REG(
		data->zone[nr].off_desired));
	if ( nr == 0 || nr == 1 ) {
		lm85_write_value(client, LM85_REG_AFAN_HYST1,
			(data->zone[0].hyst << 4)
			| data->zone[1].hyst
			);
	} else {
		lm85_write_value(client, LM85_REG_AFAN_HYST2,
			(data->zone[2].hyst << 4)
		);
	}
	mutex_unlock(&data->update_lock);
	return count;
}
static ssize_t show_temp_auto_temp_max(struct device *dev, char *buf, int nr)
{
	struct lm85_data *data = lm85_update_device(dev);
	return sprintf(buf,"%d\n", TEMP_FROM_REG(data->zone[nr].limit) +
		RANGE_FROM_REG(data->zone[nr].range));
}
static ssize_t set_temp_auto_temp_max(struct device *dev, const char *buf,
	size_t count, int nr)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct lm85_data *data = i2c_get_clientdata(client);
	int min;
	long val = simple_strtol(buf, NULL, 10);

	mutex_lock(&data->update_lock);
	min = TEMP_FROM_REG(data->zone[nr].limit);
	data->zone[nr].max_desired = TEMP_TO_REG(val);
	data->zone[nr].range = RANGE_TO_REG(
		val - min);
	lm85_write_value(client, LM85_REG_AFAN_RANGE(nr),
		((data->zone[nr].range & 0x0f) << 4)
		| (data->autofan[nr].freq & 0x07));
	mutex_unlock(&data->update_lock);
	return count;
}
static ssize_t show_temp_auto_temp_crit(struct device *dev, char *buf, int nr)
{
	struct lm85_data *data = lm85_update_device(dev);
	return sprintf(buf,"%d\n", TEMP_FROM_REG(data->zone[nr].critical));
}
static ssize_t set_temp_auto_temp_crit(struct device *dev, const char *buf,
		size_t count, int nr)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct lm85_data *data = i2c_get_clientdata(client);
	long val = simple_strtol(buf, NULL, 10);

	mutex_lock(&data->update_lock);
	data->zone[nr].critical = TEMP_TO_REG(val);
	lm85_write_value(client, LM85_REG_AFAN_CRITICAL(nr),
		data->zone[nr].critical);
	mutex_unlock(&data->update_lock);
	return count;
}
#define temp_auto(offset)						\
static ssize_t show_temp##offset##_auto_temp_off (struct device *dev, struct device_attribute *attr,	\
	char *buf)							\
{									\
	return show_temp_auto_temp_off(dev, buf, offset - 1);		\
}									\
static ssize_t set_temp##offset##_auto_temp_off (struct device *dev, struct device_attribute *attr,	\
	const char *buf, size_t count)					\
{									\
	return set_temp_auto_temp_off(dev, buf, count, offset - 1);	\
}									\
static ssize_t show_temp##offset##_auto_temp_min (struct device *dev, struct device_attribute *attr,	\
	char *buf)							\
{									\
	return show_temp_auto_temp_min(dev, buf, offset - 1);		\
}									\
static ssize_t set_temp##offset##_auto_temp_min (struct device *dev, struct device_attribute *attr,	\
	const char *buf, size_t count)					\
{									\
	return set_temp_auto_temp_min(dev, buf, count, offset - 1);	\
}									\
static ssize_t show_temp##offset##_auto_temp_max (struct device *dev, struct device_attribute *attr,	\
	char *buf)							\
{									\
	return show_temp_auto_temp_max(dev, buf, offset - 1);		\
}									\
static ssize_t set_temp##offset##_auto_temp_max (struct device *dev, struct device_attribute *attr,	\
	const char *buf, size_t count)					\
{									\
	return set_temp_auto_temp_max(dev, buf, count, offset - 1);	\
}									\
static ssize_t show_temp##offset##_auto_temp_crit (struct device *dev, struct device_attribute *attr,	\
	char *buf)							\
{									\
	return show_temp_auto_temp_crit(dev, buf, offset - 1);		\
}									\
static ssize_t set_temp##offset##_auto_temp_crit (struct device *dev, struct device_attribute *attr,	\
	const char *buf, size_t count)					\
{									\
	return set_temp_auto_temp_crit(dev, buf, count, offset - 1);	\
}									\
static DEVICE_ATTR(temp##offset##_auto_temp_off, S_IRUGO | S_IWUSR,	\
		show_temp##offset##_auto_temp_off,			\
		set_temp##offset##_auto_temp_off);			\
static DEVICE_ATTR(temp##offset##_auto_temp_min, S_IRUGO | S_IWUSR,	\
		show_temp##offset##_auto_temp_min,			\
		set_temp##offset##_auto_temp_min);			\
static DEVICE_ATTR(temp##offset##_auto_temp_max, S_IRUGO | S_IWUSR,	\
		show_temp##offset##_auto_temp_max,			\
		set_temp##offset##_auto_temp_max);			\
static DEVICE_ATTR(temp##offset##_auto_temp_crit, S_IRUGO | S_IWUSR,	\
		show_temp##offset##_auto_temp_crit,			\
		set_temp##offset##_auto_temp_crit);
temp_auto(1);
temp_auto(2);
temp_auto(3);

static int lm85_attach_adapter(struct i2c_adapter *adapter)
{
	if (!(adapter->class & I2C_CLASS_HWMON))
		return 0;
	return i2c_probe(adapter, &addr_data, lm85_detect);
}

static int lm85_detect(struct i2c_adapter *adapter, int address,
		int kind)
{
	int company, verstep ;
	struct i2c_client *new_client = NULL;
	struct lm85_data *data;
	int err = 0;
	const char *type_name = "";

	if (!i2c_check_functionality(adapter,
					I2C_FUNC_SMBUS_BYTE_DATA)) {
		/* We need to be able to do byte I/O */
		goto ERROR0 ;
	};

	/* OK. For now, we presume we have a valid client. We now create the
	   client structure, even though we cannot fill it completely yet.
	   But it allows us to access lm85_{read,write}_value. */

	if (!(data = kzalloc(sizeof(struct lm85_data), GFP_KERNEL))) {
		err = -ENOMEM;
		goto ERROR0;
	}

	new_client = &data->client;
	i2c_set_clientdata(new_client, data);
	new_client->addr = address;
	new_client->adapter = adapter;
	new_client->driver = &lm85_driver;
	new_client->flags = 0;

	/* Now, we do the remaining detection. */

	company = lm85_read_value(new_client, LM85_REG_COMPANY);
	verstep = lm85_read_value(new_client, LM85_REG_VERSTEP);

	dev_dbg(&adapter->dev, "Detecting device at %d,0x%02x with"
		" COMPANY: 0x%02x and VERSTEP: 0x%02x\n",
		i2c_adapter_id(new_client->adapter), new_client->addr,
		company, verstep);

	/* If auto-detecting, Determine the chip type. */
	if (kind <= 0) {
		dev_dbg(&adapter->dev, "Autodetecting device at %d,0x%02x ...\n",
			i2c_adapter_id(adapter), address );
		if( company == LM85_COMPANY_NATIONAL
		    && verstep == LM85_VERSTEP_LM85C ) {
			kind = lm85c ;
		} else if( company == LM85_COMPANY_NATIONAL
		    && verstep == LM85_VERSTEP_LM85B ) {
			kind = lm85b ;
		} else if( company == LM85_COMPANY_NATIONAL
		    && (verstep & LM85_VERSTEP_VMASK) == LM85_VERSTEP_GENERIC ) {
			dev_err(&adapter->dev, "Unrecognized version/stepping 0x%02x"
				" Defaulting to LM85.\n", verstep);
			kind = any_chip ;
		} else if( company == LM85_COMPANY_ANALOG_DEV
		    && verstep == LM85_VERSTEP_ADM1027 ) {
			kind = adm1027 ;
		} else if( company == LM85_COMPANY_ANALOG_DEV
		    && (verstep == LM85_VERSTEP_ADT7463
			 || verstep == LM85_VERSTEP_ADT7463C) ) {
			kind = adt7463 ;
		} else if( company == LM85_COMPANY_ANALOG_DEV
		    && (verstep & LM85_VERSTEP_VMASK) == LM85_VERSTEP_GENERIC ) {
			dev_err(&adapter->dev, "Unrecognized version/stepping 0x%02x"
				" Defaulting to Generic LM85.\n", verstep );
			kind = any_chip ;
		} else if( company == LM85_COMPANY_SMSC
		    && (verstep == LM85_VERSTEP_EMC6D100_A0
			 || verstep == LM85_VERSTEP_EMC6D100_A1) ) {
			/* Unfortunately, we can't tell a '100 from a '101
			 * from the registers.  Since a '101 is a '100
			 * in a package with fewer pins and therefore no
			 * 3.3V, 1.5V or 1.8V inputs, perhaps if those
			 * inputs read 0, then it's a '101.
			 */
			kind = emc6d100 ;
		} else if( company == LM85_COMPANY_SMSC
		    && verstep == LM85_VERSTEP_EMC6D102) {
			kind = emc6d102 ;
		} else if( company == LM85_COMPANY_SMSC
		    && (verstep & LM85_VERSTEP_VMASK) == LM85_VERSTEP_GENERIC) {
			dev_err(&adapter->dev, "lm85: Detected SMSC chip\n");
			dev_err(&adapter->dev, "lm85: Unrecognized version/stepping 0x%02x"
			    " Defaulting to Generic LM85.\n", verstep );
			kind = any_chip ;
		} else if( kind == any_chip
		    && (verstep & LM85_VERSTEP_VMASK) == LM85_VERSTEP_GENERIC) {
			dev_err(&adapter->dev, "Generic LM85 Version 6 detected\n");
			/* Leave kind as "any_chip" */
		} else {
			dev_dbg(&adapter->dev, "Autodetection failed\n");
			/* Not an LM85 ... */
			if( kind == any_chip ) {  /* User used force=x,y */
				dev_err(&adapter->dev, "Generic LM85 Version 6 not"
					" found at %d,0x%02x. Try force_lm85c.\n",
					i2c_adapter_id(adapter), address );
			}
			err = 0 ;
			goto ERROR1;
		}
	}

	/* Fill in the chip specific driver values */
	if ( kind == any_chip ) {
		type_name = "lm85";
	} else if ( kind == lm85b ) {
		type_name = "lm85b";
	} else if ( kind == lm85c ) {
		type_name = "lm85c";
	} else if ( kind == adm1027 ) {
		type_name = "adm1027";
	} else if ( kind == adt7463 ) {
		type_name = "adt7463";
	} else if ( kind == emc6d100){
		type_name = "emc6d100";
	} else if ( kind == emc6d102 ) {
		type_name = "emc6d102";
	}
	strlcpy(new_client->name, type_name, I2C_NAME_SIZE);

	/* Fill in the remaining client fields */
	data->type = kind;
	data->valid = 0;
	mutex_init(&data->update_lock);

	/* Tell the I2C layer a new client has arrived */
	if ((err = i2c_attach_client(new_client)))
		goto ERROR1;

	/* Set the VRM version */
	data->vrm = vid_which_vrm();

	/* Initialize the LM85 chip */
	lm85_init_client(new_client);

	/* Register sysfs hooks */
	data->class_dev = hwmon_device_register(&new_client->dev);
	if (IS_ERR(data->class_dev)) {
		err = PTR_ERR(data->class_dev);
		goto ERROR2;
	}

	device_create_file(&new_client->dev, &dev_attr_fan1_input);
	device_create_file(&new_client->dev, &dev_attr_fan2_input);
	device_create_file(&new_client->dev, &dev_attr_fan3_input);
	device_create_file(&new_client->dev, &dev_attr_fan4_input);
	device_create_file(&new_client->dev, &dev_attr_fan1_min);
	device_create_file(&new_client->dev, &dev_attr_fan2_min);
	device_create_file(&new_client->dev, &dev_attr_fan3_min);
	device_create_file(&new_client->dev, &dev_attr_fan4_min);
	device_create_file(&new_client->dev, &dev_attr_pwm1);
	device_create_file(&new_client->dev, &dev_attr_pwm2);
	device_create_file(&new_client->dev, &dev_attr_pwm3);
	device_create_file(&new_client->dev, &dev_attr_pwm1_enable);
	device_create_file(&new_client->dev, &dev_attr_pwm2_enable);
	device_create_file(&new_client->dev, &dev_attr_pwm3_enable);
	device_create_file(&new_client->dev, &dev_attr_in0_input);
	device_create_file(&new_client->dev, &dev_attr_in1_input);
	device_create_file(&new_client->dev, &dev_attr_in2_input);
	device_create_file(&new_client->dev, &dev_attr_in3_input);
	device_create_file(&new_client->dev, &dev_attr_in0_min);
	device_create_file(&new_client->dev, &dev_attr_in1_min);
	device_create_file(&new_client->dev, &dev_attr_in2_min);
	device_create_file(&new_client->dev, &dev_attr_in3_min);
	device_create_file(&new_client->dev, &dev_attr_in0_max);
	device_create_file(&new_client->dev, &dev_attr_in1_max);
	device_create_file(&new_client->dev, &dev_attr_in2_max);
	device_create_file(&new_client->dev, &dev_attr_in3_max);
	device_create_file(&new_client->dev, &dev_attr_temp1_input);
	device_create_file(&new_client->dev, &dev_attr_temp2_input);
	device_create_file(&new_client->dev, &dev_attr_temp3_input);
	device_create_file(&new_client->dev, &dev_attr_temp1_min);
	device_create_file(&new_client->dev, &dev_attr_temp2_min);
	device_create_file(&new_client->dev, &dev_attr_temp3_min);
	device_create_file(&new_client->dev, &dev_attr_temp1_max);
	device_create_file(&new_client->dev, &dev_attr_temp2_max);
	device_create_file(&new_client->dev, &dev_attr_temp3_max);
	device_create_file(&new_client->dev, &dev_attr_vrm);
	device_create_file(&new_client->dev, &dev_attr_cpu0_vid);
	device_create_file(&new_client->dev, &dev_attr_alarms);
	device_create_file(&new_client->dev, &dev_attr_pwm1_auto_channels);
	device_create_file(&new_client->dev, &dev_attr_pwm2_auto_channels);
	device_create_file(&new_client->dev, &dev_attr_pwm3_auto_channels);
	device_create_file(&new_client->dev, &dev_attr_pwm1_auto_pwm_min);
	device_create_file(&new_client->dev, &dev_attr_pwm2_auto_pwm_min);
	device_create_file(&new_client->dev, &dev_attr_pwm3_auto_pwm_min);
	device_create_file(&new_client->dev, &dev_attr_pwm1_auto_pwm_minctl);
	device_create_file(&new_client->dev, &dev_attr_pwm2_auto_pwm_minctl);
	device_create_file(&new_client->dev, &dev_attr_pwm3_auto_pwm_minctl);
	device_create_file(&new_client->dev, &dev_attr_pwm1_auto_pwm_freq);
	device_create_file(&new_client->dev, &dev_attr_pwm2_auto_pwm_freq);
	device_create_file(&new_client->dev, &dev_attr_pwm3_auto_pwm_freq);
	device_create_file(&new_client->dev, &dev_attr_temp1_auto_temp_off);
	device_create_file(&new_client->dev, &dev_attr_temp2_auto_temp_off);
	device_create_file(&new_client->dev, &dev_attr_temp3_auto_temp_off);
	device_create_file(&new_client->dev, &dev_attr_temp1_auto_temp_min);
	device_create_file(&new_client->dev, &dev_attr_temp2_auto_temp_min);
	device_create_file(&new_client->dev, &dev_attr_temp3_auto_temp_min);
	device_create_file(&new_client->dev, &dev_attr_temp1_auto_temp_max);
	device_create_file(&new_client->dev, &dev_attr_temp2_auto_temp_max);
	device_create_file(&new_client->dev, &dev_attr_temp3_auto_temp_max);
	device_create_file(&new_client->dev, &dev_attr_temp1_auto_temp_crit);
	device_create_file(&new_client->dev, &dev_attr_temp2_auto_temp_crit);
	device_create_file(&new_client->dev, &dev_attr_temp3_auto_temp_crit);

	/* The ADT7463 has an optional VRM 10 mode where pin 21 is used
	   as a sixth digital VID input rather than an analog input. */
	data->vid = lm85_read_value(new_client, LM85_REG_VID);
	if (!(kind == adt7463 && (data->vid & 0x80))) {
		device_create_file(&new_client->dev, &dev_attr_in4_input);
		device_create_file(&new_client->dev, &dev_attr_in4_min);
		device_create_file(&new_client->dev, &dev_attr_in4_max);
	}

	return 0;

	/* Error out and cleanup code */
    ERROR2:
	i2c_detach_client(new_client);
    ERROR1:
	kfree(data);
    ERROR0:
	return err;
}

static int lm85_detach_client(struct i2c_client *client)
{
	struct lm85_data *data = i2c_get_clientdata(client);
	hwmon_device_unregister(data->class_dev);
	i2c_detach_client(client);
	kfree(data);
	return 0;
}


static int lm85_read_value(struct i2c_client *client, u8 reg)
{
	int res;

	/* What size location is it? */
	switch( reg ) {
	case LM85_REG_FAN(0) :  /* Read WORD data */
	case LM85_REG_FAN(1) :
	case LM85_REG_FAN(2) :
	case LM85_REG_FAN(3) :
	case LM85_REG_FAN_MIN(0) :
	case LM85_REG_FAN_MIN(1) :
	case LM85_REG_FAN_MIN(2) :
	case LM85_REG_FAN_MIN(3) :
	case LM85_REG_ALARM1 :	/* Read both bytes at once */
		res = i2c_smbus_read_byte_data(client, reg) & 0xff ;
		res |= i2c_smbus_read_byte_data(client, reg+1) << 8 ;
		break ;
	case ADT7463_REG_TMIN_CTL1 :  /* Read WORD MSB, LSB */
		res = i2c_smbus_read_byte_data(client, reg) << 8 ;
		res |= i2c_smbus_read_byte_data(client, reg+1) & 0xff ;
		break ;
	default:	/* Read BYTE data */
		res = i2c_smbus_read_byte_data(client, reg);
		break ;
	}

	return res ;
}

static int lm85_write_value(struct i2c_client *client, u8 reg, int value)
{
	int res ;

	switch( reg ) {
	case LM85_REG_FAN(0) :  /* Write WORD data */
	case LM85_REG_FAN(1) :
	case LM85_REG_FAN(2) :
	case LM85_REG_FAN(3) :
	case LM85_REG_FAN_MIN(0) :
	case LM85_REG_FAN_MIN(1) :
	case LM85_REG_FAN_MIN(2) :
	case LM85_REG_FAN_MIN(3) :
	/* NOTE: ALARM is read only, so not included here */
		res = i2c_smbus_write_byte_data(client, reg, value & 0xff) ;
		res |= i2c_smbus_write_byte_data(client, reg+1, (value>>8) & 0xff) ;
		break ;
	case ADT7463_REG_TMIN_CTL1 :  /* Write WORD MSB, LSB */
		res = i2c_smbus_write_byte_data(client, reg, (value>>8) & 0xff);
		res |= i2c_smbus_write_byte_data(client, reg+1, value & 0xff) ;
		break ;
	default:	/* Write BYTE data */
		res = i2c_smbus_write_byte_data(client, reg, value);
		break ;
	}

	return res ;
}

static void lm85_init_client(struct i2c_client *client)
{
	int value;
	struct lm85_data *data = i2c_get_clientdata(client);

	dev_dbg(&client->dev, "Initializing device\n");

	/* Warn if part was not "READY" */
	value = lm85_read_value(client, LM85_REG_CONFIG);
	dev_dbg(&client->dev, "LM85_REG_CONFIG is: 0x%02x\n", value);
	if( value & 0x02 ) {
		dev_err(&client->dev, "Client (%d,0x%02x) config is locked.\n",
			    i2c_adapter_id(client->adapter), client->addr );
	};
	if( ! (value & 0x04) ) {
		dev_err(&client->dev, "Client (%d,0x%02x) is not ready.\n",
			    i2c_adapter_id(client->adapter), client->addr );
	};
	if( value & 0x10
	    && ( data->type == adm1027
		|| data->type == adt7463 ) ) {
		dev_err(&client->dev, "Client (%d,0x%02x) VxI mode is set.  "
			"Please report this to the lm85 maintainer.\n",
			    i2c_adapter_id(client->adapter), client->addr );
	};

	/* WE INTENTIONALLY make no changes to the limits,
	 *   offsets, pwms, fans and zones.  If they were
	 *   configured, we don't want to mess with them.
	 *   If they weren't, the default is 100% PWM, no
	 *   control and will suffice until 'sensors -s'
	 *   can be run by the user.
	 */

	/* Start monitoring */
	value = lm85_read_value(client, LM85_REG_CONFIG);
	/* Try to clear LOCK, Set START, save everything else */
	value = (value & ~ 0x02) | 0x01 ;
	dev_dbg(&client->dev, "Setting CONFIG to: 0x%02x\n", value);
	lm85_write_value(client, LM85_REG_CONFIG, value);
}

static struct lm85_data *lm85_update_device(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct lm85_data *data = i2c_get_clientdata(client);
	int i;

	mutex_lock(&data->update_lock);

	if ( !data->valid ||
	     time_after(jiffies, data->last_reading + LM85_DATA_INTERVAL) ) {
		/* Things that change quickly */
		dev_dbg(&client->dev, "Reading sensor values\n");
		
		/* Have to read extended bits first to "freeze" the
		 * more significant bits that are read later.
		 */
		if ( (data->type == adm1027) || (data->type == adt7463) ) {
			int ext1 = lm85_read_value(client,
						   ADM1027_REG_EXTEND_ADC1);
			int ext2 =  lm85_read_value(client,
						    ADM1027_REG_EXTEND_ADC2);
			int val = (ext1 << 8) + ext2;

			for(i = 0; i <= 4; i++)
				data->in_ext[i] = (val>>(i * 2))&0x03;

			for(i = 0; i <= 2; i++)
				data->temp_ext[i] = (val>>((i + 5) * 2))&0x03;
		}

		/* adc_scale is 2^(number of LSBs). There are 4 extra bits in
		   the emc6d102 and 2 in the adt7463 and adm1027. In all
		   other chips ext is always 0 and the value of scale is
		   irrelevant. So it is left in 4*/
		data->adc_scale = (data->type == emc6d102 ) ? 16 : 4;

		data->vid = lm85_read_value(client, LM85_REG_VID);

		for (i = 0; i <= 3; ++i) {
			data->in[i] =
			    lm85_read_value(client, LM85_REG_IN(i));
		}

		if (!(data->type == adt7463 && (data->vid & 0x80))) {
			data->in[4] = lm85_read_value(client,
				      LM85_REG_IN(4));
		}

		for (i = 0; i <= 3; ++i) {
			data->fan[i] =
			    lm85_read_value(client, LM85_REG_FAN(i));
		}

		for (i = 0; i <= 2; ++i) {
			data->temp[i] =
			    lm85_read_value(client, LM85_REG_TEMP(i));
		}

		for (i = 0; i <= 2; ++i) {
			data->pwm[i] =
			    lm85_read_value(client, LM85_REG_PWM(i));
		}

		data->alarms = lm85_read_value(client, LM85_REG_ALARM1);

		if ( data->type == adt7463 ) {
			if( data->therm_total < ULONG_MAX - 256 ) {
			    data->therm_total +=
				lm85_read_value(client, ADT7463_REG_THERM );
			}
		} else if ( data->type == emc6d100 ) {
			/* Three more voltage sensors */
			for (i = 5; i <= 7; ++i) {
				data->in[i] =
					lm85_read_value(client, EMC6D100_REG_IN(i));
			}
			/* More alarm bits */
			data->alarms |=
				lm85_read_value(client, EMC6D100_REG_ALARM3) << 16;
		} else if (data->type == emc6d102 ) {
			/* Have to read LSB bits after the MSB ones because
			   the reading of the MSB bits has frozen the
			   LSBs (backward from the ADM1027).
			 */
			int ext1 = lm85_read_value(client,
						   EMC6D102_REG_EXTEND_ADC1);
			int ext2 = lm85_read_value(client,
						   EMC6D102_REG_EXTEND_ADC2);
			int ext3 = lm85_read_value(client,
						   EMC6D102_REG_EXTEND_ADC3);
			int ext4 = lm85_read_value(client,
						   EMC6D102_REG_EXTEND_ADC4);
			data->in_ext[0] = ext3 & 0x0f;
			data->in_ext[1] = ext4 & 0x0f;
			data->in_ext[2] = (ext4 >> 4) & 0x0f;
			data->in_ext[3] = (ext3 >> 4) & 0x0f;
			data->in_ext[4] = (ext2 >> 4) & 0x0f;

			data->temp_ext[0] = ext1 & 0x0f;
			data->temp_ext[1] = ext2 & 0x0f;
			data->temp_ext[2] = (ext1 >> 4) & 0x0f;
		}

		data->last_reading = jiffies ;
	};  /* last_reading */

	if ( !data->valid ||
	     time_after(jiffies, data->last_config + LM85_CONFIG_INTERVAL) ) {
		/* Things that don't change often */
		dev_dbg(&client->dev, "Reading config values\n");

		for (i = 0; i <= 3; ++i) {
			data->in_min[i] =
			    lm85_read_value(client, LM85_REG_IN_MIN(i));
			data->in_max[i] =
			    lm85_read_value(client, LM85_REG_IN_MAX(i));
		}

		if (!(data->type == adt7463 && (data->vid & 0x80))) {
			data->in_min[4] = lm85_read_value(client,
					  LM85_REG_IN_MIN(4));
			data->in_max[4] = lm85_read_value(client,
					  LM85_REG_IN_MAX(4));
		}

		if ( data->type == emc6d100 ) {
			for (i = 5; i <= 7; ++i) {
				data->in_min[i] =
					lm85_read_value(client, EMC6D100_REG_IN_MIN(i));
				data->in_max[i] =
					lm85_read_value(client, EMC6D100_REG_IN_MAX(i));
			}
		}

		for (i = 0; i <= 3; ++i) {
			data->fan_min[i] =
			    lm85_read_value(client, LM85_REG_FAN_MIN(i));
		}

		for (i = 0; i <= 2; ++i) {
			data->temp_min[i] =
			    lm85_read_value(client, LM85_REG_TEMP_MIN(i));
			data->temp_max[i] =
			    lm85_read_value(client, LM85_REG_TEMP_MAX(i));
		}

		for (i = 0; i <= 2; ++i) {
			int val ;
			data->autofan[i].config =
			    lm85_read_value(client, LM85_REG_AFAN_CONFIG(i));
			val = lm85_read_value(client, LM85_REG_AFAN_RANGE(i));
			data->autofan[i].freq = val & 0x07 ;
			data->zone[i].range = (val >> 4) & 0x0f ;
			data->autofan[i].min_pwm =
			    lm85_read_value(client, LM85_REG_AFAN_MINPWM(i));
			data->zone[i].limit =
			    lm85_read_value(client, LM85_REG_AFAN_LIMIT(i));
			data->zone[i].critical =
			    lm85_read_value(client, LM85_REG_AFAN_CRITICAL(i));
		}

		i = lm85_read_value(client, LM85_REG_AFAN_SPIKE1);
		data->smooth[0] = i & 0x0f ;
		data->syncpwm3 = i & 0x10 ;  /* Save PWM3 config */
		data->autofan[0].min_off = (i & 0x20) != 0 ;
		data->autofan[1].min_off = (i & 0x40) != 0 ;
		data->autofan[2].min_off = (i & 0x80) != 0 ;
		i = lm85_read_value(client, LM85_REG_AFAN_SPIKE2);
		data->smooth[1] = (i>>4) & 0x0f ;
		data->smooth[2] = i & 0x0f ;

		i = lm85_read_value(client, LM85_REG_AFAN_HYST1);
		data->zone[0].hyst = (i>>4) & 0x0f ;
		data->zone[1].hyst = i & 0x0f ;

		i = lm85_read_value(client, LM85_REG_AFAN_HYST2);
		data->zone[2].hyst = (i>>4) & 0x0f ;

		if ( (data->type == lm85b) || (data->type == lm85c) ) {
			data->tach_mode = lm85_read_value(client,
				LM85_REG_TACH_MODE );
			data->spinup_ctl = lm85_read_value(client,
				LM85_REG_SPINUP_CTL );
		} else if ( (data->type == adt7463) || (data->type == adm1027) ) {
			if ( data->type == adt7463 ) {
				for (i = 0; i <= 2; ++i) {
				    data->oppoint[i] = lm85_read_value(client,
					ADT7463_REG_OPPOINT(i) );
				}
				data->tmin_ctl = lm85_read_value(client,
					ADT7463_REG_TMIN_CTL1 );
				data->therm_limit = lm85_read_value(client,
					ADT7463_REG_THERM_LIMIT );
			}
			for (i = 0; i <= 2; ++i) {
			    data->temp_offset[i] = lm85_read_value(client,
				ADM1027_REG_TEMP_OFFSET(i) );
			}
			data->tach_mode = lm85_read_value(client,
				ADM1027_REG_CONFIG3 );
			data->fan_ppr = lm85_read_value(client,
				ADM1027_REG_FAN_PPR );
		}
	
		data->last_config = jiffies;
	};  /* last_config */

	data->valid = 1;

	mutex_unlock(&data->update_lock);

	return data;
}


static int __init sm_lm85_init(void)
{
	return i2c_add_driver(&lm85_driver);
}

static void  __exit sm_lm85_exit(void)
{
	i2c_del_driver(&lm85_driver);
}

/* Thanks to Richard Barrington for adding the LM85 to sensors-detect.
 * Thanks to Margit Schubert-While <margitsw@t-online.de> for help with
 *     post 2.7.0 CVS changes.
 */
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Philip Pokorny <ppokorny@penguincomputing.com>, Margit Schubert-While <margitsw@t-online.de>, Justin Thiessen <jthiessen@penguincomputing.com");
MODULE_DESCRIPTION("LM85-B, LM85-C driver");

module_init(sm_lm85_init);
module_exit(sm_lm85_exit);
