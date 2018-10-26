/*
 * 1-Wire implementation for the ds2438 chip
 *
 * Copyright (c) 2017 Mariusz Bialonczyk <manio@skyboo.net>
 *
 * This source code is licensed under the GNU General Public License,
 * Version 2. See the file COPYING for more details.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/types.h>
#include <linux/delay.h>

#include <linux/w1.h>

#define W1_FAMILY_DS2438		0x26

#define W1_DS2438_RETRIES		3

/* Memory commands */
#define W1_DS2438_READ_SCRATCH		0xBE
#define W1_DS2438_WRITE_SCRATCH		0x4E
#define W1_DS2438_COPY_SCRATCH		0x48
#define W1_DS2438_RECALL_MEMORY		0xB8
/* Register commands */
#define W1_DS2438_CONVERT_TEMP		0x44
#define W1_DS2438_CONVERT_VOLTAGE	0xB4

#define DS2438_PAGE_SIZE		8
#define DS2438_ADC_INPUT_VAD		0
#define DS2438_ADC_INPUT_VDD		1
#define DS2438_MAX_CONVERSION_TIME	10		/* ms */

/* Page #0 definitions */
#define DS2438_STATUS_REG		0x00		/* Status/Configuration Register */
#define DS2438_STATUS_IAD		(1 << 0)	/* Current A/D Control Bit */
#define DS2438_STATUS_CA		(1 << 1)	/* Current Accumulator Configuration */
#define DS2438_STATUS_EE		(1 << 2)	/* Current Accumulator Shadow Selector bit */
#define DS2438_STATUS_AD		(1 << 3)	/* Voltage A/D Input Select Bit */
#define DS2438_STATUS_TB		(1 << 4)	/* Temperature Busy Flag */
#define DS2438_STATUS_NVB		(1 << 5)	/* Nonvolatile Memory Busy Flag */
#define DS2438_STATUS_ADB		(1 << 6)	/* A/D Converter Busy Flag */

#define DS2438_TEMP_LSB			0x01
#define DS2438_TEMP_MSB			0x02
#define DS2438_VOLTAGE_LSB		0x03
#define DS2438_VOLTAGE_MSB		0x04
#define DS2438_CURRENT_LSB		0x05
#define DS2438_CURRENT_MSB		0x06
#define DS2438_THRESHOLD		0x07

static int w1_ds2438_get_page(struct w1_slave *sl, int pageno, u8 *buf)
{
	unsigned int retries = W1_DS2438_RETRIES;
	u8 w1_buf[2];
	u8 crc;
	size_t count;

	while (retries--) {
		crc = 0;

		if (w1_reset_select_slave(sl))
			continue;
		w1_buf[0] = W1_DS2438_RECALL_MEMORY;
		w1_buf[1] = 0x00;
		w1_write_block(sl->master, w1_buf, 2);

		if (w1_reset_select_slave(sl))
			continue;
		w1_buf[0] = W1_DS2438_READ_SCRATCH;
		w1_buf[1] = 0x00;
		w1_write_block(sl->master, w1_buf, 2);

		count = w1_read_block(sl->master, buf, DS2438_PAGE_SIZE + 1);
		if (count == DS2438_PAGE_SIZE + 1) {
			crc = w1_calc_crc8(buf, DS2438_PAGE_SIZE);

			/* check for correct CRC */
			if ((u8)buf[DS2438_PAGE_SIZE] == crc)
				return 0;
		}
	}
	return -1;
}

static int w1_ds2438_get_temperature(struct w1_slave *sl, int16_t *temperature)
{
	unsigned int retries = W1_DS2438_RETRIES;
	u8 w1_buf[DS2438_PAGE_SIZE + 1 /*for CRC*/];
	unsigned int tm = DS2438_MAX_CONVERSION_TIME;
	unsigned long sleep_rem;
	int ret;

	mutex_lock(&sl->master->bus_mutex);

	while (retries--) {
		if (w1_reset_select_slave(sl))
			continue;
		w1_write_8(sl->master, W1_DS2438_CONVERT_TEMP);

		mutex_unlock(&sl->master->bus_mutex);
		sleep_rem = msleep_interruptible(tm);
		if (sleep_rem != 0) {
			ret = -1;
			goto post_unlock;
		}

		if (mutex_lock_interruptible(&sl->master->bus_mutex) != 0) {
			ret = -1;
			goto post_unlock;
		}

		break;
	}

	if (w1_ds2438_get_page(sl, 0, w1_buf) == 0) {
		*temperature = (((int16_t) w1_buf[DS2438_TEMP_MSB]) << 8) | ((uint16_t) w1_buf[DS2438_TEMP_LSB]);
		ret = 0;
	} else
		ret = -1;

	mutex_unlock(&sl->master->bus_mutex);

post_unlock:
	return ret;
}

static int w1_ds2438_change_config_bit(struct w1_slave *sl, u8 mask, u8 value)
{
	unsigned int retries = W1_DS2438_RETRIES;
	u8 w1_buf[3];
	u8 status;
	int perform_write = 0;

	while (retries--) {
		if (w1_reset_select_slave(sl))
			continue;
		w1_buf[0] = W1_DS2438_RECALL_MEMORY;
		w1_buf[1] = 0x00;
		w1_write_block(sl->master, w1_buf, 2);

		if (w1_reset_select_slave(sl))
			continue;
		w1_buf[0] = W1_DS2438_READ_SCRATCH;
		w1_buf[1] = 0x00;
		w1_write_block(sl->master, w1_buf, 2);

		/* reading one byte of result */
		status = w1_read_8(sl->master);

		/* if bit0=1, set a value to a mask for easy compare */
		if (value)
			value = mask;

		if ((status & mask) == value)
			return 0;	/* already set as requested */
		else {
			/* changing bit */
			status ^= mask;
			perform_write = 1;
		}
		break;
	}

	if (perform_write) {
		retries = W1_DS2438_RETRIES;
		while (retries--) {
			if (w1_reset_select_slave(sl))
				continue;
			w1_buf[0] = W1_DS2438_WRITE_SCRATCH;
			w1_buf[1] = 0x00;
			w1_buf[2] = status;
			w1_write_block(sl->master, w1_buf, 3);

			if (w1_reset_select_slave(sl))
				continue;
			w1_buf[0] = W1_DS2438_COPY_SCRATCH;
			w1_buf[1] = 0x00;
			w1_write_block(sl->master, w1_buf, 2);

			return 0;
		}
	}
	return -1;
}

static int w1_ds2438_get_voltage(struct w1_slave *sl,
				 int adc_input, uint16_t *voltage)
{
	unsigned int retries = W1_DS2438_RETRIES;
	u8 w1_buf[DS2438_PAGE_SIZE + 1 /*for CRC*/];
	unsigned int tm = DS2438_MAX_CONVERSION_TIME;
	unsigned long sleep_rem;
	int ret;

	mutex_lock(&sl->master->bus_mutex);

	if (w1_ds2438_change_config_bit(sl, DS2438_STATUS_AD, adc_input)) {
		ret = -1;
		goto pre_unlock;
	}

	while (retries--) {
		if (w1_reset_select_slave(sl))
			continue;
		w1_write_8(sl->master, W1_DS2438_CONVERT_VOLTAGE);

		mutex_unlock(&sl->master->bus_mutex);
		sleep_rem = msleep_interruptible(tm);
		if (sleep_rem != 0) {
			ret = -1;
			goto post_unlock;
		}

		if (mutex_lock_interruptible(&sl->master->bus_mutex) != 0) {
			ret = -1;
			goto post_unlock;
		}

		break;
	}

	if (w1_ds2438_get_page(sl, 0, w1_buf) == 0) {
		*voltage = (((uint16_t) w1_buf[DS2438_VOLTAGE_MSB]) << 8) | ((uint16_t) w1_buf[DS2438_VOLTAGE_LSB]);
		ret = 0;
	} else
		ret = -1;

pre_unlock:
	mutex_unlock(&sl->master->bus_mutex);

post_unlock:
	return ret;
}

static int w1_ds2438_get_current(struct w1_slave *sl, int16_t *voltage)
{
	u8 w1_buf[DS2438_PAGE_SIZE + 1 /*for CRC*/];
	int ret;

	mutex_lock(&sl->master->bus_mutex);

	if (w1_ds2438_get_page(sl, 0, w1_buf) == 0) {
		/* The voltage measured across current sense resistor RSENS. */
		*voltage = (((int16_t) w1_buf[DS2438_CURRENT_MSB]) << 8) | ((int16_t) w1_buf[DS2438_CURRENT_LSB]);
		ret = 0;
	} else
		ret = -1;

	mutex_unlock(&sl->master->bus_mutex);

	return ret;
}

static ssize_t iad_write(struct file *filp, struct kobject *kobj,
			 struct bin_attribute *bin_attr, char *buf,
			 loff_t off, size_t count)
{
	struct w1_slave *sl = kobj_to_w1_slave(kobj);
	int ret;

	if (count != 1 || off != 0)
		return -EFAULT;

	mutex_lock(&sl->master->bus_mutex);

	if (w1_ds2438_change_config_bit(sl, DS2438_STATUS_IAD, *buf & 0x01) == 0)
		ret = 1;
	else
		ret = -EIO;

	mutex_unlock(&sl->master->bus_mutex);

	return ret;
}

static ssize_t iad_read(struct file *filp, struct kobject *kobj,
			struct bin_attribute *bin_attr, char *buf,
			loff_t off, size_t count)
{
	struct w1_slave *sl = kobj_to_w1_slave(kobj);
	int ret;
	int16_t voltage;

	if (off != 0)
		return 0;
	if (!buf)
		return -EINVAL;

	if (w1_ds2438_get_current(sl, &voltage) == 0) {
		ret = snprintf(buf, count, "%i\n", voltage);
	} else
		ret = -EIO;

	return ret;
}

static ssize_t page0_read(struct file *filp, struct kobject *kobj,
			  struct bin_attribute *bin_attr, char *buf,
			  loff_t off, size_t count)
{
	struct w1_slave *sl = kobj_to_w1_slave(kobj);
	int ret;
	u8 w1_buf[DS2438_PAGE_SIZE + 1 /*for CRC*/];

	if (off != 0)
		return 0;
	if (!buf)
		return -EINVAL;

	mutex_lock(&sl->master->bus_mutex);

	/* Read no more than page0 size */
	if (count > DS2438_PAGE_SIZE)
		count = DS2438_PAGE_SIZE;

	if (w1_ds2438_get_page(sl, 0, w1_buf) == 0) {
		memcpy(buf, &w1_buf, count);
		ret = count;
	} else
		ret = -EIO;

	mutex_unlock(&sl->master->bus_mutex);

	return ret;
}

static ssize_t temperature_read(struct file *filp, struct kobject *kobj,
				struct bin_attribute *bin_attr, char *buf,
				loff_t off, size_t count)
{
	struct w1_slave *sl = kobj_to_w1_slave(kobj);
	int ret;
	int16_t temp;

	if (off != 0)
		return 0;
	if (!buf)
		return -EINVAL;

	if (w1_ds2438_get_temperature(sl, &temp) == 0) {
		ret = snprintf(buf, count, "%i\n", temp);
	} else
		ret = -EIO;

	return ret;
}

static ssize_t vad_read(struct file *filp, struct kobject *kobj,
			struct bin_attribute *bin_attr, char *buf,
			loff_t off, size_t count)
{
	struct w1_slave *sl = kobj_to_w1_slave(kobj);
	int ret;
	uint16_t voltage;

	if (off != 0)
		return 0;
	if (!buf)
		return -EINVAL;

	if (w1_ds2438_get_voltage(sl, DS2438_ADC_INPUT_VAD, &voltage) == 0) {
		ret = snprintf(buf, count, "%u\n", voltage);
	} else
		ret = -EIO;

	return ret;
}

static ssize_t vdd_read(struct file *filp, struct kobject *kobj,
			struct bin_attribute *bin_attr, char *buf,
			loff_t off, size_t count)
{
	struct w1_slave *sl = kobj_to_w1_slave(kobj);
	int ret;
	uint16_t voltage;

	if (off != 0)
		return 0;
	if (!buf)
		return -EINVAL;

	if (w1_ds2438_get_voltage(sl, DS2438_ADC_INPUT_VDD, &voltage) == 0) {
		ret = snprintf(buf, count, "%u\n", voltage);
	} else
		ret = -EIO;

	return ret;
}

static BIN_ATTR(iad, S_IRUGO | S_IWUSR | S_IWGRP, iad_read, iad_write, 0);
static BIN_ATTR_RO(page0, DS2438_PAGE_SIZE);
static BIN_ATTR_RO(temperature, 0/* real length varies */);
static BIN_ATTR_RO(vad, 0/* real length varies */);
static BIN_ATTR_RO(vdd, 0/* real length varies */);

static struct bin_attribute *w1_ds2438_bin_attrs[] = {
	&bin_attr_iad,
	&bin_attr_page0,
	&bin_attr_temperature,
	&bin_attr_vad,
	&bin_attr_vdd,
	NULL,
};

static const struct attribute_group w1_ds2438_group = {
	.bin_attrs = w1_ds2438_bin_attrs,
};

static const struct attribute_group *w1_ds2438_groups[] = {
	&w1_ds2438_group,
	NULL,
};

static struct w1_family_ops w1_ds2438_fops = {
	.groups		= w1_ds2438_groups,
};

static struct w1_family w1_ds2438_family = {
	.fid = W1_FAMILY_DS2438,
	.fops = &w1_ds2438_fops,
};
module_w1_family(w1_ds2438_family);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Mariusz Bialonczyk <manio@skyboo.net>");
MODULE_DESCRIPTION("1-wire driver for Maxim/Dallas DS2438 Smart Battery Monitor");
MODULE_ALIAS("w1-family-" __stringify(W1_FAMILY_DS2438));
