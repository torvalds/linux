/*
 * sht15.c - support for the SHT15 Temperature and Humidity Sensor
 *
 * Portions Copyright (c) 2010-2012 Savoir-faire Linux Inc.
 *          Jerome Oufella <jerome.oufella@savoirfairelinux.com>
 *          Vivien Didelot <vivien.didelot@savoirfairelinux.com>
 *
 * Copyright (c) 2009 Jonathan Cameron
 *
 * Copyright (c) 2007 Wouter Horre
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * For further information, see the Documentation/hwmon/sht15 file.
 */

#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/sched.h>
#include <linux/delay.h>
#include <linux/jiffies.h>
#include <linux/err.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <linux/atomic.h>
#include <linux/bitrev.h>
#include <linux/gpio/consumer.h>
#include <linux/of.h>

/* Commands */
#define SHT15_MEASURE_TEMP		0x03
#define SHT15_MEASURE_RH		0x05
#define SHT15_WRITE_STATUS		0x06
#define SHT15_READ_STATUS		0x07
#define SHT15_SOFT_RESET		0x1E

/* Min timings */
#define SHT15_TSCKL			100	/* (nsecs) clock low */
#define SHT15_TSCKH			100	/* (nsecs) clock high */
#define SHT15_TSU			150	/* (nsecs) data setup time */
#define SHT15_TSRST			11	/* (msecs) soft reset time */

/* Status Register Bits */
#define SHT15_STATUS_LOW_RESOLUTION	0x01
#define SHT15_STATUS_NO_OTP_RELOAD	0x02
#define SHT15_STATUS_HEATER		0x04
#define SHT15_STATUS_LOW_BATTERY	0x40

/* List of supported chips */
enum sht15_chips { sht10, sht11, sht15, sht71, sht75 };

/* Actions the driver may be doing */
enum sht15_state {
	SHT15_READING_NOTHING,
	SHT15_READING_TEMP,
	SHT15_READING_HUMID
};

/**
 * struct sht15_temppair - elements of voltage dependent temp calc
 * @vdd:	supply voltage in microvolts
 * @d1:		see data sheet
 */
struct sht15_temppair {
	int vdd; /* microvolts */
	int d1;
};

/* Table 9 from datasheet - relates temperature calculation to supply voltage */
static const struct sht15_temppair temppoints[] = {
	{ 2500000, -39400 },
	{ 3000000, -39600 },
	{ 3500000, -39700 },
	{ 4000000, -39800 },
	{ 5000000, -40100 },
};

/* Table from CRC datasheet, section 2.4 */
static const u8 sht15_crc8_table[] = {
	0,	49,	98,	83,	196,	245,	166,	151,
	185,	136,	219,	234,	125,	76,	31,	46,
	67,	114,	33,	16,	135,	182,	229,	212,
	250,	203,	152,	169,	62,	15,	92,	109,
	134,	183,	228,	213,	66,	115,	32,	17,
	63,	14,	93,	108,	251,	202,	153,	168,
	197,	244,	167,	150,	1,	48,	99,	82,
	124,	77,	30,	47,	184,	137,	218,	235,
	61,	12,	95,	110,	249,	200,	155,	170,
	132,	181,	230,	215,	64,	113,	34,	19,
	126,	79,	28,	45,	186,	139,	216,	233,
	199,	246,	165,	148,	3,	50,	97,	80,
	187,	138,	217,	232,	127,	78,	29,	44,
	2,	51,	96,	81,	198,	247,	164,	149,
	248,	201,	154,	171,	60,	13,	94,	111,
	65,	112,	35,	18,	133,	180,	231,	214,
	122,	75,	24,	41,	190,	143,	220,	237,
	195,	242,	161,	144,	7,	54,	101,	84,
	57,	8,	91,	106,	253,	204,	159,	174,
	128,	177,	226,	211,	68,	117,	38,	23,
	252,	205,	158,	175,	56,	9,	90,	107,
	69,	116,	39,	22,	129,	176,	227,	210,
	191,	142,	221,	236,	123,	74,	25,	40,
	6,	55,	100,	85,	194,	243,	160,	145,
	71,	118,	37,	20,	131,	178,	225,	208,
	254,	207,	156,	173,	58,	11,	88,	105,
	4,	53,	102,	87,	192,	241,	162,	147,
	189,	140,	223,	238,	121,	72,	27,	42,
	193,	240,	163,	146,	5,	52,	103,	86,
	120,	73,	26,	43,	188,	141,	222,	239,
	130,	179,	224,	209,	70,	119,	36,	21,
	59,	10,	89,	104,	255,	206,	157,	172
};

/**
 * struct sht15_data - device instance specific data
 * @sck:		clock GPIO line
 * @data:		data GPIO line
 * @read_work:		bh of interrupt handler.
 * @wait_queue:		wait queue for getting values from device.
 * @val_temp:		last temperature value read from device.
 * @val_humid:		last humidity value read from device.
 * @val_status:		last status register value read from device.
 * @checksum_ok:	last value read from the device passed CRC validation.
 * @checksumming:	flag used to enable the data validation with CRC.
 * @state:		state identifying the action the driver is doing.
 * @measurements_valid:	are the current stored measures valid (start condition).
 * @status_valid:	is the current stored status valid (start condition).
 * @last_measurement:	time of last measure.
 * @last_status:	time of last status reading.
 * @read_lock:		mutex to ensure only one read in progress at a time.
 * @dev:		associate device structure.
 * @hwmon_dev:		device associated with hwmon subsystem.
 * @reg:		associated regulator (if specified).
 * @nb:			notifier block to handle notifications of voltage
 *                      changes.
 * @supply_uv:		local copy of supply voltage used to allow use of
 *                      regulator consumer if available.
 * @supply_uv_valid:	indicates that an updated value has not yet been
 *			obtained from the regulator and so any calculations
 *			based upon it will be invalid.
 * @update_supply_work:	work struct that is used to update the supply_uv.
 * @interrupt_handled:	flag used to indicate a handler has been scheduled.
 */
struct sht15_data {
	struct gpio_desc		*sck;
	struct gpio_desc		*data;
	struct work_struct		read_work;
	wait_queue_head_t		wait_queue;
	uint16_t			val_temp;
	uint16_t			val_humid;
	u8				val_status;
	bool				checksum_ok;
	bool				checksumming;
	enum sht15_state		state;
	bool				measurements_valid;
	bool				status_valid;
	unsigned long			last_measurement;
	unsigned long			last_status;
	struct mutex			read_lock;
	struct device			*dev;
	struct device			*hwmon_dev;
	struct regulator		*reg;
	struct notifier_block		nb;
	int				supply_uv;
	bool				supply_uv_valid;
	struct work_struct		update_supply_work;
	atomic_t			interrupt_handled;
};

/**
 * sht15_crc8() - compute crc8
 * @data:	sht15 specific data.
 * @value:	sht15 retrieved data.
 *
 * This implements section 2 of the CRC datasheet.
 */
static u8 sht15_crc8(struct sht15_data *data,
		const u8 *value,
		int len)
{
	u8 crc = bitrev8(data->val_status & 0x0F);

	while (len--) {
		crc = sht15_crc8_table[*value ^ crc];
		value++;
	}

	return crc;
}

/**
 * sht15_connection_reset() - reset the comms interface
 * @data:	sht15 specific data
 *
 * This implements section 3.4 of the data sheet
 */
static int sht15_connection_reset(struct sht15_data *data)
{
	int i, err;

	err = gpiod_direction_output(data->data, 1);
	if (err)
		return err;
	ndelay(SHT15_TSCKL);
	gpiod_set_value(data->sck, 0);
	ndelay(SHT15_TSCKL);
	for (i = 0; i < 9; ++i) {
		gpiod_set_value(data->sck, 1);
		ndelay(SHT15_TSCKH);
		gpiod_set_value(data->sck, 0);
		ndelay(SHT15_TSCKL);
	}
	return 0;
}

/**
 * sht15_send_bit() - send an individual bit to the device
 * @data:	device state data
 * @val:	value of bit to be sent
 */
static inline void sht15_send_bit(struct sht15_data *data, int val)
{
	gpiod_set_value(data->data, val);
	ndelay(SHT15_TSU);
	gpiod_set_value(data->sck, 1);
	ndelay(SHT15_TSCKH);
	gpiod_set_value(data->sck, 0);
	ndelay(SHT15_TSCKL); /* clock low time */
}

/**
 * sht15_transmission_start() - specific sequence for new transmission
 * @data:	device state data
 *
 * Timings for this are not documented on the data sheet, so very
 * conservative ones used in implementation. This implements
 * figure 12 on the data sheet.
 */
static int sht15_transmission_start(struct sht15_data *data)
{
	int err;

	/* ensure data is high and output */
	err = gpiod_direction_output(data->data, 1);
	if (err)
		return err;
	ndelay(SHT15_TSU);
	gpiod_set_value(data->sck, 0);
	ndelay(SHT15_TSCKL);
	gpiod_set_value(data->sck, 1);
	ndelay(SHT15_TSCKH);
	gpiod_set_value(data->data, 0);
	ndelay(SHT15_TSU);
	gpiod_set_value(data->sck, 0);
	ndelay(SHT15_TSCKL);
	gpiod_set_value(data->sck, 1);
	ndelay(SHT15_TSCKH);
	gpiod_set_value(data->data, 1);
	ndelay(SHT15_TSU);
	gpiod_set_value(data->sck, 0);
	ndelay(SHT15_TSCKL);
	return 0;
}

/**
 * sht15_send_byte() - send a single byte to the device
 * @data:	device state
 * @byte:	value to be sent
 */
static void sht15_send_byte(struct sht15_data *data, u8 byte)
{
	int i;

	for (i = 0; i < 8; i++) {
		sht15_send_bit(data, !!(byte & 0x80));
		byte <<= 1;
	}
}

/**
 * sht15_wait_for_response() - checks for ack from device
 * @data:	device state
 */
static int sht15_wait_for_response(struct sht15_data *data)
{
	int err;

	err = gpiod_direction_input(data->data);
	if (err)
		return err;
	gpiod_set_value(data->sck, 1);
	ndelay(SHT15_TSCKH);
	if (gpiod_get_value(data->data)) {
		gpiod_set_value(data->sck, 0);
		dev_err(data->dev, "Command not acknowledged\n");
		err = sht15_connection_reset(data);
		if (err)
			return err;
		return -EIO;
	}
	gpiod_set_value(data->sck, 0);
	ndelay(SHT15_TSCKL);
	return 0;
}

/**
 * sht15_send_cmd() - Sends a command to the device.
 * @data:	device state
 * @cmd:	command byte to be sent
 *
 * On entry, sck is output low, data is output pull high
 * and the interrupt disabled.
 */
static int sht15_send_cmd(struct sht15_data *data, u8 cmd)
{
	int err;

	err = sht15_transmission_start(data);
	if (err)
		return err;
	sht15_send_byte(data, cmd);
	return sht15_wait_for_response(data);
}

/**
 * sht15_soft_reset() - send a soft reset command
 * @data:	sht15 specific data.
 *
 * As described in section 3.2 of the datasheet.
 */
static int sht15_soft_reset(struct sht15_data *data)
{
	int ret;

	ret = sht15_send_cmd(data, SHT15_SOFT_RESET);
	if (ret)
		return ret;
	msleep(SHT15_TSRST);
	/* device resets default hardware status register value */
	data->val_status = 0;

	return ret;
}

/**
 * sht15_ack() - send a ack
 * @data:	sht15 specific data.
 *
 * Each byte of data is acknowledged by pulling the data line
 * low for one clock pulse.
 */
static int sht15_ack(struct sht15_data *data)
{
	int err;

	err = gpiod_direction_output(data->data, 0);
	if (err)
		return err;
	ndelay(SHT15_TSU);
	gpiod_set_value(data->sck, 1);
	ndelay(SHT15_TSU);
	gpiod_set_value(data->sck, 0);
	ndelay(SHT15_TSU);
	gpiod_set_value(data->data, 1);

	return gpiod_direction_input(data->data);
}

/**
 * sht15_end_transmission() - notify device of end of transmission
 * @data:	device state.
 *
 * This is basically a NAK (single clock pulse, data high).
 */
static int sht15_end_transmission(struct sht15_data *data)
{
	int err;

	err = gpiod_direction_output(data->data, 1);
	if (err)
		return err;
	ndelay(SHT15_TSU);
	gpiod_set_value(data->sck, 1);
	ndelay(SHT15_TSCKH);
	gpiod_set_value(data->sck, 0);
	ndelay(SHT15_TSCKL);
	return 0;
}

/**
 * sht15_read_byte() - Read a byte back from the device
 * @data:	device state.
 */
static u8 sht15_read_byte(struct sht15_data *data)
{
	int i;
	u8 byte = 0;

	for (i = 0; i < 8; ++i) {
		byte <<= 1;
		gpiod_set_value(data->sck, 1);
		ndelay(SHT15_TSCKH);
		byte |= !!gpiod_get_value(data->data);
		gpiod_set_value(data->sck, 0);
		ndelay(SHT15_TSCKL);
	}
	return byte;
}

/**
 * sht15_send_status() - write the status register byte
 * @data:	sht15 specific data.
 * @status:	the byte to set the status register with.
 *
 * As described in figure 14 and table 5 of the datasheet.
 */
static int sht15_send_status(struct sht15_data *data, u8 status)
{
	int err;

	err = sht15_send_cmd(data, SHT15_WRITE_STATUS);
	if (err)
		return err;
	err = gpiod_direction_output(data->data, 1);
	if (err)
		return err;
	ndelay(SHT15_TSU);
	sht15_send_byte(data, status);
	err = sht15_wait_for_response(data);
	if (err)
		return err;

	data->val_status = status;
	return 0;
}

/**
 * sht15_update_status() - get updated status register from device if too old
 * @data:	device instance specific data.
 *
 * As described in figure 15 and table 5 of the datasheet.
 */
static int sht15_update_status(struct sht15_data *data)
{
	int ret = 0;
	u8 status;
	u8 previous_config;
	u8 dev_checksum = 0;
	u8 checksum_vals[2];
	int timeout = HZ;

	mutex_lock(&data->read_lock);
	if (time_after(jiffies, data->last_status + timeout)
			|| !data->status_valid) {
		ret = sht15_send_cmd(data, SHT15_READ_STATUS);
		if (ret)
			goto unlock;
		status = sht15_read_byte(data);

		if (data->checksumming) {
			sht15_ack(data);
			dev_checksum = bitrev8(sht15_read_byte(data));
			checksum_vals[0] = SHT15_READ_STATUS;
			checksum_vals[1] = status;
			data->checksum_ok = (sht15_crc8(data, checksum_vals, 2)
					== dev_checksum);
		}

		ret = sht15_end_transmission(data);
		if (ret)
			goto unlock;

		/*
		 * Perform checksum validation on the received data.
		 * Specification mentions that in case a checksum verification
		 * fails, a soft reset command must be sent to the device.
		 */
		if (data->checksumming && !data->checksum_ok) {
			previous_config = data->val_status & 0x07;
			ret = sht15_soft_reset(data);
			if (ret)
				goto unlock;
			if (previous_config) {
				ret = sht15_send_status(data, previous_config);
				if (ret) {
					dev_err(data->dev,
						"CRC validation failed, unable "
						"to restore device settings\n");
					goto unlock;
				}
			}
			ret = -EAGAIN;
			goto unlock;
		}

		data->val_status = status;
		data->status_valid = true;
		data->last_status = jiffies;
	}

unlock:
	mutex_unlock(&data->read_lock);
	return ret;
}

/**
 * sht15_measurement() - get a new value from device
 * @data:		device instance specific data
 * @command:		command sent to request value
 * @timeout_msecs:	timeout after which comms are assumed
 *			to have failed are reset.
 */
static int sht15_measurement(struct sht15_data *data,
			     int command,
			     int timeout_msecs)
{
	int ret;
	u8 previous_config;

	ret = sht15_send_cmd(data, command);
	if (ret)
		return ret;

	ret = gpiod_direction_input(data->data);
	if (ret)
		return ret;
	atomic_set(&data->interrupt_handled, 0);

	enable_irq(gpiod_to_irq(data->data));
	if (gpiod_get_value(data->data) == 0) {
		disable_irq_nosync(gpiod_to_irq(data->data));
		/* Only relevant if the interrupt hasn't occurred. */
		if (!atomic_read(&data->interrupt_handled))
			schedule_work(&data->read_work);
	}
	ret = wait_event_timeout(data->wait_queue,
				 (data->state == SHT15_READING_NOTHING),
				 msecs_to_jiffies(timeout_msecs));
	if (data->state != SHT15_READING_NOTHING) { /* I/O error occurred */
		data->state = SHT15_READING_NOTHING;
		return -EIO;
	} else if (ret == 0) { /* timeout occurred */
		disable_irq_nosync(gpiod_to_irq(data->data));
		ret = sht15_connection_reset(data);
		if (ret)
			return ret;
		return -ETIME;
	}

	/*
	 *  Perform checksum validation on the received data.
	 *  Specification mentions that in case a checksum verification fails,
	 *  a soft reset command must be sent to the device.
	 */
	if (data->checksumming && !data->checksum_ok) {
		previous_config = data->val_status & 0x07;
		ret = sht15_soft_reset(data);
		if (ret)
			return ret;
		if (previous_config) {
			ret = sht15_send_status(data, previous_config);
			if (ret) {
				dev_err(data->dev,
					"CRC validation failed, unable "
					"to restore device settings\n");
				return ret;
			}
		}
		return -EAGAIN;
	}

	return 0;
}

/**
 * sht15_update_measurements() - get updated measures from device if too old
 * @data:	device state
 */
static int sht15_update_measurements(struct sht15_data *data)
{
	int ret = 0;
	int timeout = HZ;

	mutex_lock(&data->read_lock);
	if (time_after(jiffies, data->last_measurement + timeout)
	    || !data->measurements_valid) {
		data->state = SHT15_READING_HUMID;
		ret = sht15_measurement(data, SHT15_MEASURE_RH, 160);
		if (ret)
			goto unlock;
		data->state = SHT15_READING_TEMP;
		ret = sht15_measurement(data, SHT15_MEASURE_TEMP, 400);
		if (ret)
			goto unlock;
		data->measurements_valid = true;
		data->last_measurement = jiffies;
	}

unlock:
	mutex_unlock(&data->read_lock);
	return ret;
}

/**
 * sht15_calc_temp() - convert the raw reading to a temperature
 * @data:	device state
 *
 * As per section 4.3 of the data sheet.
 */
static inline int sht15_calc_temp(struct sht15_data *data)
{
	int d1 = temppoints[0].d1;
	int d2 = (data->val_status & SHT15_STATUS_LOW_RESOLUTION) ? 40 : 10;
	int i;

	for (i = ARRAY_SIZE(temppoints) - 1; i > 0; i--)
		/* Find pointer to interpolate */
		if (data->supply_uv > temppoints[i - 1].vdd) {
			d1 = (data->supply_uv - temppoints[i - 1].vdd)
				* (temppoints[i].d1 - temppoints[i - 1].d1)
				/ (temppoints[i].vdd - temppoints[i - 1].vdd)
				+ temppoints[i - 1].d1;
			break;
		}

	return data->val_temp * d2 + d1;
}

/**
 * sht15_calc_humid() - using last temperature convert raw to humid
 * @data:	device state
 *
 * This is the temperature compensated version as per section 4.2 of
 * the data sheet.
 *
 * The sensor is assumed to be V3, which is compatible with V4.
 * Humidity conversion coefficients are shown in table 7 of the datasheet.
 */
static inline int sht15_calc_humid(struct sht15_data *data)
{
	int rh_linear; /* milli percent */
	int temp = sht15_calc_temp(data);
	int c2, c3;
	int t2;
	const int c1 = -4;

	if (data->val_status & SHT15_STATUS_LOW_RESOLUTION) {
		c2 = 648000; /* x 10 ^ -6 */
		c3 = -7200;  /* x 10 ^ -7 */
		t2 = 1280;
	} else {
		c2 = 40500;  /* x 10 ^ -6 */
		c3 = -28;    /* x 10 ^ -7 */
		t2 = 80;
	}

	rh_linear = c1 * 1000
		+ c2 * data->val_humid / 1000
		+ (data->val_humid * data->val_humid * c3) / 10000;
	return (temp - 25000) * (10000 + t2 * data->val_humid)
		/ 1000000 + rh_linear;
}

/**
 * sht15_show_status() - show status information in sysfs
 * @dev:	device.
 * @attr:	device attribute.
 * @buf:	sysfs buffer where information is written to.
 *
 * Will be called on read access to temp1_fault, humidity1_fault
 * and heater_enable sysfs attributes.
 * Returns number of bytes written into buffer, negative errno on error.
 */
static ssize_t sht15_show_status(struct device *dev,
				 struct device_attribute *attr,
				 char *buf)
{
	int ret;
	struct sht15_data *data = dev_get_drvdata(dev);
	u8 bit = to_sensor_dev_attr(attr)->index;

	ret = sht15_update_status(data);

	return ret ? ret : sprintf(buf, "%d\n", !!(data->val_status & bit));
}

/**
 * sht15_store_heater() - change heater state via sysfs
 * @dev:	device.
 * @attr:	device attribute.
 * @buf:	sysfs buffer to read the new heater state from.
 * @count:	length of the data.
 *
 * Will be called on write access to heater_enable sysfs attribute.
 * Returns number of bytes actually decoded, negative errno on error.
 */
static ssize_t sht15_store_heater(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t count)
{
	int ret;
	struct sht15_data *data = dev_get_drvdata(dev);
	long value;
	u8 status;

	if (kstrtol(buf, 10, &value))
		return -EINVAL;

	mutex_lock(&data->read_lock);
	status = data->val_status & 0x07;
	if (!!value)
		status |= SHT15_STATUS_HEATER;
	else
		status &= ~SHT15_STATUS_HEATER;

	ret = sht15_send_status(data, status);
	mutex_unlock(&data->read_lock);

	return ret ? ret : count;
}

/**
 * sht15_show_temp() - show temperature measurement value in sysfs
 * @dev:	device.
 * @attr:	device attribute.
 * @buf:	sysfs buffer where measurement values are written to.
 *
 * Will be called on read access to temp1_input sysfs attribute.
 * Returns number of bytes written into buffer, negative errno on error.
 */
static ssize_t sht15_show_temp(struct device *dev,
			       struct device_attribute *attr,
			       char *buf)
{
	int ret;
	struct sht15_data *data = dev_get_drvdata(dev);

	/* Technically no need to read humidity as well */
	ret = sht15_update_measurements(data);

	return ret ? ret : sprintf(buf, "%d\n",
				   sht15_calc_temp(data));
}

/**
 * sht15_show_humidity() - show humidity measurement value in sysfs
 * @dev:	device.
 * @attr:	device attribute.
 * @buf:	sysfs buffer where measurement values are written to.
 *
 * Will be called on read access to humidity1_input sysfs attribute.
 * Returns number of bytes written into buffer, negative errno on error.
 */
static ssize_t sht15_show_humidity(struct device *dev,
				   struct device_attribute *attr,
				   char *buf)
{
	int ret;
	struct sht15_data *data = dev_get_drvdata(dev);

	ret = sht15_update_measurements(data);

	return ret ? ret : sprintf(buf, "%d\n", sht15_calc_humid(data));
}

static ssize_t name_show(struct device *dev,
			 struct device_attribute *attr,
			 char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	return sprintf(buf, "%s\n", pdev->name);
}

static SENSOR_DEVICE_ATTR(temp1_input, S_IRUGO,
			  sht15_show_temp, NULL, 0);
static SENSOR_DEVICE_ATTR(humidity1_input, S_IRUGO,
			  sht15_show_humidity, NULL, 0);
static SENSOR_DEVICE_ATTR(temp1_fault, S_IRUGO, sht15_show_status, NULL,
			  SHT15_STATUS_LOW_BATTERY);
static SENSOR_DEVICE_ATTR(humidity1_fault, S_IRUGO, sht15_show_status, NULL,
			  SHT15_STATUS_LOW_BATTERY);
static SENSOR_DEVICE_ATTR(heater_enable, S_IRUGO | S_IWUSR, sht15_show_status,
			  sht15_store_heater, SHT15_STATUS_HEATER);
static DEVICE_ATTR_RO(name);
static struct attribute *sht15_attrs[] = {
	&sensor_dev_attr_temp1_input.dev_attr.attr,
	&sensor_dev_attr_humidity1_input.dev_attr.attr,
	&sensor_dev_attr_temp1_fault.dev_attr.attr,
	&sensor_dev_attr_humidity1_fault.dev_attr.attr,
	&sensor_dev_attr_heater_enable.dev_attr.attr,
	&dev_attr_name.attr,
	NULL,
};

static const struct attribute_group sht15_attr_group = {
	.attrs = sht15_attrs,
};

static irqreturn_t sht15_interrupt_fired(int irq, void *d)
{
	struct sht15_data *data = d;

	/* First disable the interrupt */
	disable_irq_nosync(irq);
	atomic_inc(&data->interrupt_handled);
	/* Then schedule a reading work struct */
	if (data->state != SHT15_READING_NOTHING)
		schedule_work(&data->read_work);
	return IRQ_HANDLED;
}

static void sht15_bh_read_data(struct work_struct *work_s)
{
	uint16_t val = 0;
	u8 dev_checksum = 0;
	u8 checksum_vals[3];
	struct sht15_data *data
		= container_of(work_s, struct sht15_data,
			       read_work);

	/* Firstly, verify the line is low */
	if (gpiod_get_value(data->data)) {
		/*
		 * If not, then start the interrupt again - care here as could
		 * have gone low in meantime so verify it hasn't!
		 */
		atomic_set(&data->interrupt_handled, 0);
		enable_irq(gpiod_to_irq(data->data));
		/* If still not occurred or another handler was scheduled */
		if (gpiod_get_value(data->data)
		    || atomic_read(&data->interrupt_handled))
			return;
	}

	/* Read the data back from the device */
	val = sht15_read_byte(data);
	val <<= 8;
	if (sht15_ack(data))
		goto wakeup;
	val |= sht15_read_byte(data);

	if (data->checksumming) {
		/*
		 * Ask the device for a checksum and read it back.
		 * Note: the device sends the checksum byte reversed.
		 */
		if (sht15_ack(data))
			goto wakeup;
		dev_checksum = bitrev8(sht15_read_byte(data));
		checksum_vals[0] = (data->state == SHT15_READING_TEMP) ?
			SHT15_MEASURE_TEMP : SHT15_MEASURE_RH;
		checksum_vals[1] = (u8) (val >> 8);
		checksum_vals[2] = (u8) val;
		data->checksum_ok
			= (sht15_crc8(data, checksum_vals, 3) == dev_checksum);
	}

	/* Tell the device we are done */
	if (sht15_end_transmission(data))
		goto wakeup;

	switch (data->state) {
	case SHT15_READING_TEMP:
		data->val_temp = val;
		break;
	case SHT15_READING_HUMID:
		data->val_humid = val;
		break;
	default:
		break;
	}

	data->state = SHT15_READING_NOTHING;
wakeup:
	wake_up(&data->wait_queue);
}

static void sht15_update_voltage(struct work_struct *work_s)
{
	struct sht15_data *data
		= container_of(work_s, struct sht15_data,
			       update_supply_work);
	data->supply_uv = regulator_get_voltage(data->reg);
}

/**
 * sht15_invalidate_voltage() - mark supply voltage invalid when notified by reg
 * @nb:		associated notification structure
 * @event:	voltage regulator state change event code
 * @ignored:	function parameter - ignored here
 *
 * Note that as the notification code holds the regulator lock, we have
 * to schedule an update of the supply voltage rather than getting it directly.
 */
static int sht15_invalidate_voltage(struct notifier_block *nb,
				    unsigned long event,
				    void *ignored)
{
	struct sht15_data *data = container_of(nb, struct sht15_data, nb);

	if (event == REGULATOR_EVENT_VOLTAGE_CHANGE)
		data->supply_uv_valid = false;
	schedule_work(&data->update_supply_work);

	return NOTIFY_OK;
}

#ifdef CONFIG_OF
static const struct of_device_id sht15_dt_match[] = {
	{ .compatible = "sensirion,sht15" },
	{ },
};
MODULE_DEVICE_TABLE(of, sht15_dt_match);
#endif

static int sht15_probe(struct platform_device *pdev)
{
	int ret;
	struct sht15_data *data;

	data = devm_kzalloc(&pdev->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	INIT_WORK(&data->read_work, sht15_bh_read_data);
	INIT_WORK(&data->update_supply_work, sht15_update_voltage);
	platform_set_drvdata(pdev, data);
	mutex_init(&data->read_lock);
	data->dev = &pdev->dev;
	init_waitqueue_head(&data->wait_queue);

	/*
	 * If a regulator is available,
	 * query what the supply voltage actually is!
	 */
	data->reg = devm_regulator_get_optional(data->dev, "vcc");
	if (!IS_ERR(data->reg)) {
		int voltage;

		voltage = regulator_get_voltage(data->reg);
		if (voltage)
			data->supply_uv = voltage;

		ret = regulator_enable(data->reg);
		if (ret != 0) {
			dev_err(&pdev->dev,
				"failed to enable regulator: %d\n", ret);
			return ret;
		}

		/*
		 * Setup a notifier block to update this if another device
		 * causes the voltage to change
		 */
		data->nb.notifier_call = &sht15_invalidate_voltage;
		ret = regulator_register_notifier(data->reg, &data->nb);
		if (ret) {
			dev_err(&pdev->dev,
				"regulator notifier request failed\n");
			regulator_disable(data->reg);
			return ret;
		}
	}

	/* Try requesting the GPIOs */
	data->sck = devm_gpiod_get(&pdev->dev, "clk", GPIOD_OUT_LOW);
	if (IS_ERR(data->sck)) {
		ret = PTR_ERR(data->sck);
		dev_err(&pdev->dev, "clock line GPIO request failed\n");
		goto err_release_reg;
	}
	data->data = devm_gpiod_get(&pdev->dev, "data", GPIOD_IN);
	if (IS_ERR(data->data)) {
		ret = PTR_ERR(data->data);
		dev_err(&pdev->dev, "data line GPIO request failed\n");
		goto err_release_reg;
	}

	ret = devm_request_irq(&pdev->dev, gpiod_to_irq(data->data),
			       sht15_interrupt_fired,
			       IRQF_TRIGGER_FALLING,
			       "sht15 data",
			       data);
	if (ret) {
		dev_err(&pdev->dev, "failed to get irq for data line\n");
		goto err_release_reg;
	}
	disable_irq_nosync(gpiod_to_irq(data->data));
	ret = sht15_connection_reset(data);
	if (ret)
		goto err_release_reg;
	ret = sht15_soft_reset(data);
	if (ret)
		goto err_release_reg;

	ret = sysfs_create_group(&pdev->dev.kobj, &sht15_attr_group);
	if (ret) {
		dev_err(&pdev->dev, "sysfs create failed\n");
		goto err_release_reg;
	}

	data->hwmon_dev = hwmon_device_register(data->dev);
	if (IS_ERR(data->hwmon_dev)) {
		ret = PTR_ERR(data->hwmon_dev);
		goto err_release_sysfs_group;
	}

	return 0;

err_release_sysfs_group:
	sysfs_remove_group(&pdev->dev.kobj, &sht15_attr_group);
err_release_reg:
	if (!IS_ERR(data->reg)) {
		regulator_unregister_notifier(data->reg, &data->nb);
		regulator_disable(data->reg);
	}
	return ret;
}

static int sht15_remove(struct platform_device *pdev)
{
	struct sht15_data *data = platform_get_drvdata(pdev);

	/*
	 * Make sure any reads from the device are done and
	 * prevent new ones beginning
	 */
	mutex_lock(&data->read_lock);
	if (sht15_soft_reset(data)) {
		mutex_unlock(&data->read_lock);
		return -EFAULT;
	}
	hwmon_device_unregister(data->hwmon_dev);
	sysfs_remove_group(&pdev->dev.kobj, &sht15_attr_group);
	if (!IS_ERR(data->reg)) {
		regulator_unregister_notifier(data->reg, &data->nb);
		regulator_disable(data->reg);
	}

	mutex_unlock(&data->read_lock);

	return 0;
}

static const struct platform_device_id sht15_device_ids[] = {
	{ "sht10", sht10 },
	{ "sht11", sht11 },
	{ "sht15", sht15 },
	{ "sht71", sht71 },
	{ "sht75", sht75 },
	{ }
};
MODULE_DEVICE_TABLE(platform, sht15_device_ids);

static struct platform_driver sht15_driver = {
	.driver = {
		.name = "sht15",
		.of_match_table = of_match_ptr(sht15_dt_match),
	},
	.probe = sht15_probe,
	.remove = sht15_remove,
	.id_table = sht15_device_ids,
};
module_platform_driver(sht15_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Sensirion SHT15 temperature and humidity sensor driver");
