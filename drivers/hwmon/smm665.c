/*
 * Driver for SMM665 Power Controller / Monitor
 *
 * Copyright (C) 2010 Ericsson AB.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This driver should also work for SMM465, SMM764, and SMM766, but is untested
 * for those chips. Only monitoring functionality is implemented.
 *
 * Datasheets:
 * http://www.summitmicro.com/prod_select/summary/SMM665/SMM665B_2089_20.pdf
 * http://www.summitmicro.com/prod_select/summary/SMM766B/SMM766B_2122.pdf
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/delay.h>
#include <linux/jiffies.h>

/* Internal reference voltage (VREF, x 1000 */
#define SMM665_VREF_ADC_X1000	1250

/* module parameters */
static int vref = SMM665_VREF_ADC_X1000;
module_param(vref, int, 0);
MODULE_PARM_DESC(vref, "Reference voltage in mV");

enum chips { smm465, smm665, smm665c, smm764, smm766 };

/*
 * ADC channel addresses
 */
#define	SMM665_MISC16_ADC_DATA_A	0x00
#define	SMM665_MISC16_ADC_DATA_B	0x01
#define	SMM665_MISC16_ADC_DATA_C	0x02
#define	SMM665_MISC16_ADC_DATA_D	0x03
#define	SMM665_MISC16_ADC_DATA_E	0x04
#define	SMM665_MISC16_ADC_DATA_F	0x05
#define	SMM665_MISC16_ADC_DATA_VDD	0x06
#define	SMM665_MISC16_ADC_DATA_12V	0x07
#define	SMM665_MISC16_ADC_DATA_INT_TEMP	0x08
#define	SMM665_MISC16_ADC_DATA_AIN1	0x09
#define	SMM665_MISC16_ADC_DATA_AIN2	0x0a

/*
 * Command registers
 */
#define	SMM665_MISC8_CMD_STS		0x80
#define	SMM665_MISC8_STATUS1		0x81
#define	SMM665_MISC8_STATUSS2		0x82
#define	SMM665_MISC8_IO_POLARITY	0x83
#define	SMM665_MISC8_PUP_POLARITY	0x84
#define	SMM665_MISC8_ADOC_STATUS1	0x85
#define	SMM665_MISC8_ADOC_STATUS2	0x86
#define	SMM665_MISC8_WRITE_PROT		0x87
#define	SMM665_MISC8_STS_TRACK		0x88

/*
 * Configuration registers and register groups
 */
#define SMM665_ADOC_ENABLE		0x0d
#define SMM665_LIMIT_BASE		0x80	/* First limit register */

/*
 * Limit register bit masks
 */
#define SMM665_TRIGGER_RST		0x8000
#define SMM665_TRIGGER_HEALTHY		0x4000
#define SMM665_TRIGGER_POWEROFF		0x2000
#define SMM665_TRIGGER_SHUTDOWN		0x1000
#define SMM665_ADC_MASK			0x03ff

#define smm665_is_critical(lim)	((lim) & (SMM665_TRIGGER_RST \
					| SMM665_TRIGGER_POWEROFF \
					| SMM665_TRIGGER_SHUTDOWN))
/*
 * Fault register bit definitions
 * Values are merged from status registers 1/2,
 * with status register 1 providing the upper 8 bits.
 */
#define SMM665_FAULT_A		0x0001
#define SMM665_FAULT_B		0x0002
#define SMM665_FAULT_C		0x0004
#define SMM665_FAULT_D		0x0008
#define SMM665_FAULT_E		0x0010
#define SMM665_FAULT_F		0x0020
#define SMM665_FAULT_VDD	0x0040
#define SMM665_FAULT_12V	0x0080
#define SMM665_FAULT_TEMP	0x0100
#define SMM665_FAULT_AIN1	0x0200
#define SMM665_FAULT_AIN2	0x0400

/*
 * I2C Register addresses
 *
 * The configuration register needs to be the configured base register.
 * The command/status register address is derived from it.
 */
#define SMM665_REGMASK		0x78
#define SMM665_CMDREG_BASE	0x48
#define SMM665_CONFREG_BASE	0x50

/*
 *  Equations given by chip manufacturer to calculate voltage/temperature values
 *  vref = Reference voltage on VREF_ADC pin (module parameter)
 *  adc  = 10bit ADC value read back from registers
 */

/* Voltage A-F and VDD */
#define SMM665_VMON_ADC_TO_VOLTS(adc)  ((adc) * vref / 256)

/* Voltage 12VIN */
#define SMM665_12VIN_ADC_TO_VOLTS(adc) ((adc) * vref * 3 / 256)

/* Voltage AIN1, AIN2 */
#define SMM665_AIN_ADC_TO_VOLTS(adc)   ((adc) * vref / 512)

/* Temp Sensor */
#define SMM665_TEMP_ADC_TO_CELSIUS(adc) (((adc) <= 511) ?		   \
					 ((int)(adc) * 1000 / 4) :	   \
					 (((int)(adc) - 0x400) * 1000 / 4))

#define SMM665_NUM_ADC		11

/*
 * Chip dependent ADC conversion time, in uS
 */
#define SMM665_ADC_WAIT_SMM665	70
#define SMM665_ADC_WAIT_SMM766	185

struct smm665_data {
	enum chips type;
	int conversion_time;		/* ADC conversion time */
	struct device *hwmon_dev;
	struct mutex update_lock;
	bool valid;
	unsigned long last_updated;	/* in jiffies */
	u16 adc[SMM665_NUM_ADC];	/* adc values (raw) */
	u16 faults;			/* fault status */
	/* The following values are in mV */
	int critical_min_limit[SMM665_NUM_ADC];
	int alarm_min_limit[SMM665_NUM_ADC];
	int critical_max_limit[SMM665_NUM_ADC];
	int alarm_max_limit[SMM665_NUM_ADC];
	struct i2c_client *cmdreg;
};

/*
 * smm665_read16()
 *
 * Read 16 bit value from <reg>, <reg+1>. Upper 8 bits are in <reg>.
 */
static int smm665_read16(struct i2c_client *client, int reg)
{
	int rv, val;

	rv = i2c_smbus_read_byte_data(client, reg);
	if (rv < 0)
		return rv;
	val = rv << 8;
	rv = i2c_smbus_read_byte_data(client, reg + 1);
	if (rv < 0)
		return rv;
	val |= rv;
	return val;
}

/*
 * Read adc value.
 */
static int smm665_read_adc(struct smm665_data *data, int adc)
{
	struct i2c_client *client = data->cmdreg;
	int rv;
	int radc;

	/*
	 * Algorithm for reading ADC, per SMM665 datasheet
	 *
	 *  {[S][addr][W][Ack]} {[offset][Ack]} {[S][addr][R][Nack]}
	 * [wait conversion time]
	 *  {[S][addr][R][Ack]} {[datahi][Ack]} {[datalo][Ack][P]}
	 *
	 * To implement the first part of this exchange,
	 * do a full read transaction and expect a failure/Nack.
	 * This sets up the address pointer on the SMM665
	 * and starts the ADC conversion.
	 * Then do a two-byte read transaction.
	 */
	rv = i2c_smbus_read_byte_data(client, adc << 3);
	if (rv != -ENXIO) {
		/*
		 * We expect ENXIO to reflect NACK
		 * (per Documentation/i2c/fault-codes).
		 * Everything else is an error.
		 */
		dev_dbg(&client->dev,
			"Unexpected return code %d when setting ADC index", rv);
		return (rv < 0) ? rv : -EIO;
	}

	udelay(data->conversion_time);

	/*
	 * Now read two bytes.
	 *
	 * Neither i2c_smbus_read_byte() nor
	 * i2c_smbus_read_block_data() worked here,
	 * so use i2c_smbus_read_word_swapped() instead.
	 * We could also try to use i2c_master_recv(),
	 * but that is not always supported.
	 */
	rv = i2c_smbus_read_word_swapped(client, 0);
	if (rv < 0) {
		dev_dbg(&client->dev, "Failed to read ADC value: error %d", rv);
		return -1;
	}
	/*
	 * Validate/verify readback adc channel (in bit 11..14).
	 */
	radc = (rv >> 11) & 0x0f;
	if (radc != adc) {
		dev_dbg(&client->dev, "Unexpected RADC: Expected %d got %d",
			adc, radc);
		return -EIO;
	}

	return rv & SMM665_ADC_MASK;
}

static struct smm665_data *smm665_update_device(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct smm665_data *data = i2c_get_clientdata(client);
	struct smm665_data *ret = data;

	mutex_lock(&data->update_lock);

	if (time_after(jiffies, data->last_updated + HZ) || !data->valid) {
		int i, val;

		/*
		 * read status registers
		 */
		val = smm665_read16(client, SMM665_MISC8_STATUS1);
		if (unlikely(val < 0)) {
			ret = ERR_PTR(val);
			goto abort;
		}
		data->faults = val;

		/* Read adc registers */
		for (i = 0; i < SMM665_NUM_ADC; i++) {
			val = smm665_read_adc(data, i);
			if (unlikely(val < 0)) {
				ret = ERR_PTR(val);
				goto abort;
			}
			data->adc[i] = val;
		}
		data->last_updated = jiffies;
		data->valid = 1;
	}
abort:
	mutex_unlock(&data->update_lock);
	return ret;
}

/* Return converted value from given adc */
static int smm665_convert(u16 adcval, int index)
{
	int val = 0;

	switch (index) {
	case SMM665_MISC16_ADC_DATA_12V:
		val = SMM665_12VIN_ADC_TO_VOLTS(adcval & SMM665_ADC_MASK);
		break;

	case SMM665_MISC16_ADC_DATA_VDD:
	case SMM665_MISC16_ADC_DATA_A:
	case SMM665_MISC16_ADC_DATA_B:
	case SMM665_MISC16_ADC_DATA_C:
	case SMM665_MISC16_ADC_DATA_D:
	case SMM665_MISC16_ADC_DATA_E:
	case SMM665_MISC16_ADC_DATA_F:
		val = SMM665_VMON_ADC_TO_VOLTS(adcval & SMM665_ADC_MASK);
		break;

	case SMM665_MISC16_ADC_DATA_AIN1:
	case SMM665_MISC16_ADC_DATA_AIN2:
		val = SMM665_AIN_ADC_TO_VOLTS(adcval & SMM665_ADC_MASK);
		break;

	case SMM665_MISC16_ADC_DATA_INT_TEMP:
		val = SMM665_TEMP_ADC_TO_CELSIUS(adcval & SMM665_ADC_MASK);
		break;

	default:
		/* If we get here, the developer messed up */
		WARN_ON_ONCE(1);
		break;
	}

	return val;
}

static int smm665_get_min(struct device *dev, int index)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct smm665_data *data = i2c_get_clientdata(client);

	return data->alarm_min_limit[index];
}

static int smm665_get_max(struct device *dev, int index)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct smm665_data *data = i2c_get_clientdata(client);

	return data->alarm_max_limit[index];
}

static int smm665_get_lcrit(struct device *dev, int index)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct smm665_data *data = i2c_get_clientdata(client);

	return data->critical_min_limit[index];
}

static int smm665_get_crit(struct device *dev, int index)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct smm665_data *data = i2c_get_clientdata(client);

	return data->critical_max_limit[index];
}

static ssize_t smm665_show_crit_alarm(struct device *dev,
				      struct device_attribute *da, char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(da);
	struct smm665_data *data = smm665_update_device(dev);
	int val = 0;

	if (IS_ERR(data))
		return PTR_ERR(data);

	if (data->faults & (1 << attr->index))
		val = 1;

	return snprintf(buf, PAGE_SIZE, "%d\n", val);
}

static ssize_t smm665_show_input(struct device *dev,
				 struct device_attribute *da, char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(da);
	struct smm665_data *data = smm665_update_device(dev);
	int adc = attr->index;
	int val;

	if (IS_ERR(data))
		return PTR_ERR(data);

	val = smm665_convert(data->adc[adc], adc);
	return snprintf(buf, PAGE_SIZE, "%d\n", val);
}

#define SMM665_SHOW(what) \
static ssize_t smm665_show_##what(struct device *dev, \
				    struct device_attribute *da, char *buf) \
{ \
	struct sensor_device_attribute *attr = to_sensor_dev_attr(da); \
	const int val = smm665_get_##what(dev, attr->index); \
	return snprintf(buf, PAGE_SIZE, "%d\n", val); \
}

SMM665_SHOW(min);
SMM665_SHOW(max);
SMM665_SHOW(lcrit);
SMM665_SHOW(crit);

/*
 * These macros are used below in constructing device attribute objects
 * for use with sysfs_create_group() to make a sysfs device file
 * for each register.
 */

#define SMM665_ATTR(name, type, cmd_idx) \
	static SENSOR_DEVICE_ATTR(name##_##type, S_IRUGO, \
				  smm665_show_##type, NULL, cmd_idx)

/* Construct a sensor_device_attribute structure for each register */

/* Input voltages */
SMM665_ATTR(in1, input, SMM665_MISC16_ADC_DATA_12V);
SMM665_ATTR(in2, input, SMM665_MISC16_ADC_DATA_VDD);
SMM665_ATTR(in3, input, SMM665_MISC16_ADC_DATA_A);
SMM665_ATTR(in4, input, SMM665_MISC16_ADC_DATA_B);
SMM665_ATTR(in5, input, SMM665_MISC16_ADC_DATA_C);
SMM665_ATTR(in6, input, SMM665_MISC16_ADC_DATA_D);
SMM665_ATTR(in7, input, SMM665_MISC16_ADC_DATA_E);
SMM665_ATTR(in8, input, SMM665_MISC16_ADC_DATA_F);
SMM665_ATTR(in9, input, SMM665_MISC16_ADC_DATA_AIN1);
SMM665_ATTR(in10, input, SMM665_MISC16_ADC_DATA_AIN2);

/* Input voltages min */
SMM665_ATTR(in1, min, SMM665_MISC16_ADC_DATA_12V);
SMM665_ATTR(in2, min, SMM665_MISC16_ADC_DATA_VDD);
SMM665_ATTR(in3, min, SMM665_MISC16_ADC_DATA_A);
SMM665_ATTR(in4, min, SMM665_MISC16_ADC_DATA_B);
SMM665_ATTR(in5, min, SMM665_MISC16_ADC_DATA_C);
SMM665_ATTR(in6, min, SMM665_MISC16_ADC_DATA_D);
SMM665_ATTR(in7, min, SMM665_MISC16_ADC_DATA_E);
SMM665_ATTR(in8, min, SMM665_MISC16_ADC_DATA_F);
SMM665_ATTR(in9, min, SMM665_MISC16_ADC_DATA_AIN1);
SMM665_ATTR(in10, min, SMM665_MISC16_ADC_DATA_AIN2);

/* Input voltages max */
SMM665_ATTR(in1, max, SMM665_MISC16_ADC_DATA_12V);
SMM665_ATTR(in2, max, SMM665_MISC16_ADC_DATA_VDD);
SMM665_ATTR(in3, max, SMM665_MISC16_ADC_DATA_A);
SMM665_ATTR(in4, max, SMM665_MISC16_ADC_DATA_B);
SMM665_ATTR(in5, max, SMM665_MISC16_ADC_DATA_C);
SMM665_ATTR(in6, max, SMM665_MISC16_ADC_DATA_D);
SMM665_ATTR(in7, max, SMM665_MISC16_ADC_DATA_E);
SMM665_ATTR(in8, max, SMM665_MISC16_ADC_DATA_F);
SMM665_ATTR(in9, max, SMM665_MISC16_ADC_DATA_AIN1);
SMM665_ATTR(in10, max, SMM665_MISC16_ADC_DATA_AIN2);

/* Input voltages lcrit */
SMM665_ATTR(in1, lcrit, SMM665_MISC16_ADC_DATA_12V);
SMM665_ATTR(in2, lcrit, SMM665_MISC16_ADC_DATA_VDD);
SMM665_ATTR(in3, lcrit, SMM665_MISC16_ADC_DATA_A);
SMM665_ATTR(in4, lcrit, SMM665_MISC16_ADC_DATA_B);
SMM665_ATTR(in5, lcrit, SMM665_MISC16_ADC_DATA_C);
SMM665_ATTR(in6, lcrit, SMM665_MISC16_ADC_DATA_D);
SMM665_ATTR(in7, lcrit, SMM665_MISC16_ADC_DATA_E);
SMM665_ATTR(in8, lcrit, SMM665_MISC16_ADC_DATA_F);
SMM665_ATTR(in9, lcrit, SMM665_MISC16_ADC_DATA_AIN1);
SMM665_ATTR(in10, lcrit, SMM665_MISC16_ADC_DATA_AIN2);

/* Input voltages crit */
SMM665_ATTR(in1, crit, SMM665_MISC16_ADC_DATA_12V);
SMM665_ATTR(in2, crit, SMM665_MISC16_ADC_DATA_VDD);
SMM665_ATTR(in3, crit, SMM665_MISC16_ADC_DATA_A);
SMM665_ATTR(in4, crit, SMM665_MISC16_ADC_DATA_B);
SMM665_ATTR(in5, crit, SMM665_MISC16_ADC_DATA_C);
SMM665_ATTR(in6, crit, SMM665_MISC16_ADC_DATA_D);
SMM665_ATTR(in7, crit, SMM665_MISC16_ADC_DATA_E);
SMM665_ATTR(in8, crit, SMM665_MISC16_ADC_DATA_F);
SMM665_ATTR(in9, crit, SMM665_MISC16_ADC_DATA_AIN1);
SMM665_ATTR(in10, crit, SMM665_MISC16_ADC_DATA_AIN2);

/* critical alarms */
SMM665_ATTR(in1, crit_alarm, SMM665_FAULT_12V);
SMM665_ATTR(in2, crit_alarm, SMM665_FAULT_VDD);
SMM665_ATTR(in3, crit_alarm, SMM665_FAULT_A);
SMM665_ATTR(in4, crit_alarm, SMM665_FAULT_B);
SMM665_ATTR(in5, crit_alarm, SMM665_FAULT_C);
SMM665_ATTR(in6, crit_alarm, SMM665_FAULT_D);
SMM665_ATTR(in7, crit_alarm, SMM665_FAULT_E);
SMM665_ATTR(in8, crit_alarm, SMM665_FAULT_F);
SMM665_ATTR(in9, crit_alarm, SMM665_FAULT_AIN1);
SMM665_ATTR(in10, crit_alarm, SMM665_FAULT_AIN2);

/* Temperature */
SMM665_ATTR(temp1, input, SMM665_MISC16_ADC_DATA_INT_TEMP);
SMM665_ATTR(temp1, min, SMM665_MISC16_ADC_DATA_INT_TEMP);
SMM665_ATTR(temp1, max, SMM665_MISC16_ADC_DATA_INT_TEMP);
SMM665_ATTR(temp1, lcrit, SMM665_MISC16_ADC_DATA_INT_TEMP);
SMM665_ATTR(temp1, crit, SMM665_MISC16_ADC_DATA_INT_TEMP);
SMM665_ATTR(temp1, crit_alarm, SMM665_FAULT_TEMP);

/*
 * Finally, construct an array of pointers to members of the above objects,
 * as required for sysfs_create_group()
 */
static struct attribute *smm665_attributes[] = {
	&sensor_dev_attr_in1_input.dev_attr.attr,
	&sensor_dev_attr_in1_min.dev_attr.attr,
	&sensor_dev_attr_in1_max.dev_attr.attr,
	&sensor_dev_attr_in1_lcrit.dev_attr.attr,
	&sensor_dev_attr_in1_crit.dev_attr.attr,
	&sensor_dev_attr_in1_crit_alarm.dev_attr.attr,

	&sensor_dev_attr_in2_input.dev_attr.attr,
	&sensor_dev_attr_in2_min.dev_attr.attr,
	&sensor_dev_attr_in2_max.dev_attr.attr,
	&sensor_dev_attr_in2_lcrit.dev_attr.attr,
	&sensor_dev_attr_in2_crit.dev_attr.attr,
	&sensor_dev_attr_in2_crit_alarm.dev_attr.attr,

	&sensor_dev_attr_in3_input.dev_attr.attr,
	&sensor_dev_attr_in3_min.dev_attr.attr,
	&sensor_dev_attr_in3_max.dev_attr.attr,
	&sensor_dev_attr_in3_lcrit.dev_attr.attr,
	&sensor_dev_attr_in3_crit.dev_attr.attr,
	&sensor_dev_attr_in3_crit_alarm.dev_attr.attr,

	&sensor_dev_attr_in4_input.dev_attr.attr,
	&sensor_dev_attr_in4_min.dev_attr.attr,
	&sensor_dev_attr_in4_max.dev_attr.attr,
	&sensor_dev_attr_in4_lcrit.dev_attr.attr,
	&sensor_dev_attr_in4_crit.dev_attr.attr,
	&sensor_dev_attr_in4_crit_alarm.dev_attr.attr,

	&sensor_dev_attr_in5_input.dev_attr.attr,
	&sensor_dev_attr_in5_min.dev_attr.attr,
	&sensor_dev_attr_in5_max.dev_attr.attr,
	&sensor_dev_attr_in5_lcrit.dev_attr.attr,
	&sensor_dev_attr_in5_crit.dev_attr.attr,
	&sensor_dev_attr_in5_crit_alarm.dev_attr.attr,

	&sensor_dev_attr_in6_input.dev_attr.attr,
	&sensor_dev_attr_in6_min.dev_attr.attr,
	&sensor_dev_attr_in6_max.dev_attr.attr,
	&sensor_dev_attr_in6_lcrit.dev_attr.attr,
	&sensor_dev_attr_in6_crit.dev_attr.attr,
	&sensor_dev_attr_in6_crit_alarm.dev_attr.attr,

	&sensor_dev_attr_in7_input.dev_attr.attr,
	&sensor_dev_attr_in7_min.dev_attr.attr,
	&sensor_dev_attr_in7_max.dev_attr.attr,
	&sensor_dev_attr_in7_lcrit.dev_attr.attr,
	&sensor_dev_attr_in7_crit.dev_attr.attr,
	&sensor_dev_attr_in7_crit_alarm.dev_attr.attr,

	&sensor_dev_attr_in8_input.dev_attr.attr,
	&sensor_dev_attr_in8_min.dev_attr.attr,
	&sensor_dev_attr_in8_max.dev_attr.attr,
	&sensor_dev_attr_in8_lcrit.dev_attr.attr,
	&sensor_dev_attr_in8_crit.dev_attr.attr,
	&sensor_dev_attr_in8_crit_alarm.dev_attr.attr,

	&sensor_dev_attr_in9_input.dev_attr.attr,
	&sensor_dev_attr_in9_min.dev_attr.attr,
	&sensor_dev_attr_in9_max.dev_attr.attr,
	&sensor_dev_attr_in9_lcrit.dev_attr.attr,
	&sensor_dev_attr_in9_crit.dev_attr.attr,
	&sensor_dev_attr_in9_crit_alarm.dev_attr.attr,

	&sensor_dev_attr_in10_input.dev_attr.attr,
	&sensor_dev_attr_in10_min.dev_attr.attr,
	&sensor_dev_attr_in10_max.dev_attr.attr,
	&sensor_dev_attr_in10_lcrit.dev_attr.attr,
	&sensor_dev_attr_in10_crit.dev_attr.attr,
	&sensor_dev_attr_in10_crit_alarm.dev_attr.attr,

	&sensor_dev_attr_temp1_input.dev_attr.attr,
	&sensor_dev_attr_temp1_min.dev_attr.attr,
	&sensor_dev_attr_temp1_max.dev_attr.attr,
	&sensor_dev_attr_temp1_lcrit.dev_attr.attr,
	&sensor_dev_attr_temp1_crit.dev_attr.attr,
	&sensor_dev_attr_temp1_crit_alarm.dev_attr.attr,

	NULL,
};

static const struct attribute_group smm665_group = {
	.attrs = smm665_attributes,
};

static int smm665_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct i2c_adapter *adapter = client->adapter;
	struct smm665_data *data;
	int i, ret;

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA
				     | I2C_FUNC_SMBUS_WORD_DATA))
		return -ENODEV;

	if (i2c_smbus_read_byte_data(client, SMM665_ADOC_ENABLE) < 0)
		return -ENODEV;

	data = devm_kzalloc(&client->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	i2c_set_clientdata(client, data);
	mutex_init(&data->update_lock);

	data->type = id->driver_data;
	data->cmdreg = i2c_new_dummy(adapter, (client->addr & ~SMM665_REGMASK)
				     | SMM665_CMDREG_BASE);
	if (!data->cmdreg)
		return -ENOMEM;

	switch (data->type) {
	case smm465:
	case smm665:
		data->conversion_time = SMM665_ADC_WAIT_SMM665;
		break;
	case smm665c:
	case smm764:
	case smm766:
		data->conversion_time = SMM665_ADC_WAIT_SMM766;
		break;
	}

	ret = -ENODEV;
	if (i2c_smbus_read_byte_data(data->cmdreg, SMM665_MISC8_CMD_STS) < 0)
		goto out_unregister;

	/*
	 * Read limits.
	 *
	 * Limit registers start with register SMM665_LIMIT_BASE.
	 * Each channel uses 8 registers, providing four limit values
	 * per channel. Each limit value requires two registers, with the
	 * high byte in the first register and the low byte in the second
	 * register. The first two limits are under limit values, followed
	 * by two over limit values.
	 *
	 * Limit register order matches the ADC register order, so we use
	 * ADC register defines throughout the code to index limit registers.
	 *
	 * We save the first retrieved value both as "critical" and "alarm"
	 * value. The second value overwrites either the critical or the
	 * alarm value, depending on its configuration. This ensures that both
	 * critical and alarm values are initialized, even if both registers are
	 * configured as critical or non-critical.
	 */
	for (i = 0; i < SMM665_NUM_ADC; i++) {
		int val;

		val = smm665_read16(client, SMM665_LIMIT_BASE + i * 8);
		if (unlikely(val < 0))
			goto out_unregister;
		data->critical_min_limit[i] = data->alarm_min_limit[i]
		  = smm665_convert(val, i);
		val = smm665_read16(client, SMM665_LIMIT_BASE + i * 8 + 2);
		if (unlikely(val < 0))
			goto out_unregister;
		if (smm665_is_critical(val))
			data->critical_min_limit[i] = smm665_convert(val, i);
		else
			data->alarm_min_limit[i] = smm665_convert(val, i);
		val = smm665_read16(client, SMM665_LIMIT_BASE + i * 8 + 4);
		if (unlikely(val < 0))
			goto out_unregister;
		data->critical_max_limit[i] = data->alarm_max_limit[i]
		  = smm665_convert(val, i);
		val = smm665_read16(client, SMM665_LIMIT_BASE + i * 8 + 6);
		if (unlikely(val < 0))
			goto out_unregister;
		if (smm665_is_critical(val))
			data->critical_max_limit[i] = smm665_convert(val, i);
		else
			data->alarm_max_limit[i] = smm665_convert(val, i);
	}

	/* Register sysfs hooks */
	ret = sysfs_create_group(&client->dev.kobj, &smm665_group);
	if (ret)
		goto out_unregister;

	data->hwmon_dev = hwmon_device_register(&client->dev);
	if (IS_ERR(data->hwmon_dev)) {
		ret = PTR_ERR(data->hwmon_dev);
		goto out_remove_group;
	}

	return 0;

out_remove_group:
	sysfs_remove_group(&client->dev.kobj, &smm665_group);
out_unregister:
	i2c_unregister_device(data->cmdreg);
	return ret;
}

static int smm665_remove(struct i2c_client *client)
{
	struct smm665_data *data = i2c_get_clientdata(client);

	i2c_unregister_device(data->cmdreg);
	hwmon_device_unregister(data->hwmon_dev);
	sysfs_remove_group(&client->dev.kobj, &smm665_group);

	return 0;
}

static const struct i2c_device_id smm665_id[] = {
	{"smm465", smm465},
	{"smm665", smm665},
	{"smm665c", smm665c},
	{"smm764", smm764},
	{"smm766", smm766},
	{}
};

MODULE_DEVICE_TABLE(i2c, smm665_id);

/* This is the driver that will be inserted */
static struct i2c_driver smm665_driver = {
	.driver = {
		   .name = "smm665",
		   },
	.probe = smm665_probe,
	.remove = smm665_remove,
	.id_table = smm665_id,
};

module_i2c_driver(smm665_driver);

MODULE_AUTHOR("Guenter Roeck");
MODULE_DESCRIPTION("SMM665 driver");
MODULE_LICENSE("GPL");
