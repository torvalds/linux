/*
 * Driver for Silicon Labs Si570/Si571 Programmable XO/VCXO
 *
 * Copyright (C) 2010, 2011 Ericsson AB.
 * Copyright (C) 2011 Guenter Roeck.
 *
 * Author: Guenter Roeck <guenter.roeck@ericsson.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/module.h>
#include <linux/jiffies.h>
#include <linux/i2c.h>
#include <linux/err.h>
#include <linux/mutex.h>
#include <linux/delay.h>
#include <linux/log2.h>
#include <linux/slab.h>
#include <linux/i2c/si570.h>

/* Si570 registers */
#define SI570_REG_HS_N1		7
#define SI570_REG_N1_RFREQ0	8
#define SI570_REG_RFREQ1	9
#define SI570_REG_RFREQ2	10
#define SI570_REG_RFREQ3	11
#define SI570_REG_RFREQ4	12
#define SI570_REG_CONTROL	135
#define SI570_REG_FREEZE_DCO	137

#define HS_DIV_SHIFT		5
#define HS_DIV_MASK		0xe0
#define HS_DIV_OFFSET		4
#define N1_6_2_MASK		0x1f
#define N1_1_0_MASK		0xc0
#define RFREQ_37_32_MASK	0x3f

#define SI570_FOUT_FACTORY_DFLT	156250000LL
#define SI598_FOUT_FACTORY_DFLT	10000000LL

#define SI570_MIN_FREQ		10000000L
#define SI570_MAX_FREQ		1417500000L
#define SI598_MAX_FREQ		525000000L

#define FDCO_MIN		4850000000LL
#define FDCO_MAX		5670000000LL
#define FDCO_CENTER		((FDCO_MIN + FDCO_MAX) / 2)

#define SI570_CNTRL_RECALL	(1 << 0)
#define SI570_CNTRL_FREEZE_ADC	(1 << 4)
#define SI570_CNTRL_FREEZE_M	(1 << 5)
#define SI570_CNTRL_NEWFREQ	(1 << 6)
#define SI570_CNTRL_RESET	(1 << 7)

#define SI570_FREEZE_DCO	(1 << 4)
#define SI570_UNFREEZE_DCO	0xEF

struct si570_data {
	struct attribute_group attrs;
	struct mutex lock;
	u64 max_freq;
	u64 fout;		/* Factory default frequency */
	u64 fxtal;		/* Factory xtal frequency */
	unsigned int n1;
	unsigned int hs_div;
	u64 rfreq;
	u64 frequency;
};


static struct i2c_client *si570_client;


static int si570_get_defaults(struct i2c_client *client)
{
	struct si570_data *data = i2c_get_clientdata(client);
	int reg1, reg2, reg3, reg4, reg5, reg6;
	u64 fdco;

	i2c_smbus_write_byte_data(client, SI570_REG_CONTROL,
				  SI570_CNTRL_RECALL);

	reg1 = i2c_smbus_read_byte_data(client, SI570_REG_HS_N1);
	if (reg1 < 0)
		return reg1;
	reg2 = i2c_smbus_read_byte_data(client, SI570_REG_N1_RFREQ0);
	if (reg2 < 0)
		return reg2;
	reg3 = i2c_smbus_read_byte_data(client, SI570_REG_RFREQ1);
	if (reg3 < 0)
		return reg3;
	reg4 = i2c_smbus_read_byte_data(client, SI570_REG_RFREQ2);
	if (reg4 < 0)
		return reg4;
	reg5 = i2c_smbus_read_byte_data(client, SI570_REG_RFREQ3);
	if (reg5 < 0)
		return reg5;
	reg6 = i2c_smbus_read_byte_data(client, SI570_REG_RFREQ4);
	if (reg6 < 0)
		return reg6;

	data->hs_div = ((reg1 & HS_DIV_MASK) >> HS_DIV_SHIFT) + HS_DIV_OFFSET;
	data->n1 = ((reg1 & N1_6_2_MASK) << 2) + ((reg2 & N1_1_0_MASK) >> 6)
	  + 1;
	/* Handle invalid cases */
	if (data->n1 > 1)
		data->n1 &= ~1;

	data->rfreq = reg2 & RFREQ_37_32_MASK;
	data->rfreq = (data->rfreq << 8) + reg3;
	data->rfreq = (data->rfreq << 8) + reg4;
	data->rfreq = (data->rfreq << 8) + reg5;
	data->rfreq = (data->rfreq << 8) + reg6;

	/*
	 * Accept optional precision loss to avoid arithmetic overflows.
	 * Acceptable per Silicon Labs Application Note AN334.
	 */
	fdco = data->fout * data->n1 * data->hs_div;
	if (fdco >= (1LL << 36))
		data->fxtal = div64_u64((fdco << 24), (data->rfreq >> 4));
	else
		data->fxtal = div64_u64((fdco << 28), data->rfreq);

	data->frequency = data->fout;

	return 0;
}

/*
 * Update rfreq registers
 * This function must be called with update mutex lock held.
 */
static void si570_update_rfreq(struct i2c_client *client,
			       struct si570_data *data)
{
	int status;
	status = i2c_smbus_write_byte_data(client, SI570_REG_N1_RFREQ0,
				  ((data->n1 - 1) << 6)
				  | ((data->rfreq >> 32) & RFREQ_37_32_MASK));
	if (status < 0)
		dev_err(&client->dev,
			"unable to write 0x%llX to REG_N1_RFREQ0: %d\n",
			(((data->n1 - 1) << 6) | ((data->rfreq >> 32) &
			RFREQ_37_32_MASK)) & 0xff, status);
	status = i2c_smbus_write_byte_data(client, SI570_REG_RFREQ1,
				  (data->rfreq >> 24) & 0xff);
	if (status < 0)
		dev_err(&client->dev,
			"unable to write 0x%llX to REG_RFREQ1: %d\n",
			(data->rfreq >> 24) & 0xff, status);
	status = i2c_smbus_write_byte_data(client, SI570_REG_RFREQ2,
				  (data->rfreq >> 16) & 0xff);
	if (status < 0)
		dev_err(&client->dev,
			"unable to write 0x%llX to REG_RFREQ2: %d\n",
			(data->rfreq >> 16) & 0xff, status);
	status = i2c_smbus_write_byte_data(client, SI570_REG_RFREQ3,
				  (data->rfreq >> 8) & 0xff);
	if (status < 0)
		dev_err(&client->dev,
			"unable to write 0x%llX to REG_RFREQ3: %d\n",
			(data->rfreq >> 8) & 0xff, status);
	status = i2c_smbus_write_byte_data(client, SI570_REG_RFREQ4,
				  data->rfreq & 0xff);
	if (status < 0)
		dev_err(&client->dev,
			"unable to write 0x%llX to REG_RFREQ4: %d\n",
			data->rfreq & 0xff, status);
}

/*
 * Update si570 frequency for small frequency changes (< 3,500 ppm)
 * This function must be called with update mutex lock held.
 */
static int si570_set_frequency_small(struct i2c_client *client,
				     struct si570_data *data,
				     unsigned long frequency)
{
	data->frequency = frequency;
	/* This is a re-implementation of DIV_ROUND_CLOSEST
	 * using the div64_u64 function lieu of letting the compiler
	 * insert EABI calls
	 */
	data->rfreq = div64_u64((data->rfreq * frequency) +
		div64_u64(data->frequency, 2), data->frequency);
	i2c_smbus_write_byte_data(client, SI570_REG_CONTROL,
				  SI570_CNTRL_FREEZE_M);
	si570_update_rfreq(client, data);
	i2c_smbus_write_byte_data(client, SI570_REG_CONTROL, 0);

	return 0;
}

static const uint8_t si570_hs_div_values[] = { 11, 9, 7, 6, 5, 4 };

/*
 * Set si570 frequency.
 * This function must be called with update mutex lock held.
 */
static int si570_set_frequency(struct i2c_client *client,
			       struct si570_data *data,
			       unsigned long frequency)
{
	int i, n1, hs_div;
	u64 fdco, best_fdco = ULLONG_MAX;

	for (i = 0; i < ARRAY_SIZE(si570_hs_div_values); i++) {
		hs_div = si570_hs_div_values[i];
		/* Calculate lowest possible value for n1 */
		n1 = div64_u64(div64_u64(FDCO_MIN, (u64)hs_div),
			(u64)frequency);
		if (!n1 || (n1 & 1))
			n1++;
		while (n1 <= 128) {
			fdco = (u64)frequency * (u64)hs_div * (u64)n1;
			if (fdco > FDCO_MAX)
				break;
			if (fdco >= FDCO_MIN && fdco < best_fdco) {
				data->n1 = n1;
				data->hs_div = hs_div;
				data->frequency = frequency;
				data->rfreq = div64_u64((fdco << 28),
					data->fxtal);
				best_fdco = fdco;
			}
			n1 += (n1 == 1 ? 1 : 2);
		}
	}
	if (best_fdco == ULLONG_MAX) {
		dev_err(&client->dev, "error - best FDCO is out of range\n");
		return -EINVAL;
	}

	/* The DCO reg should be accessed with a read-modify-write operation
	 * per AN334
	 */
	i2c_smbus_write_byte_data(client, SI570_REG_FREEZE_DCO,
				  SI570_FREEZE_DCO);
	i2c_smbus_write_byte_data(client, SI570_REG_HS_N1,
				  ((data->hs_div - HS_DIV_OFFSET) <<
				   HS_DIV_SHIFT)
				  | (((data->n1 - 1) >> 2) & N1_6_2_MASK));
	si570_update_rfreq(client, data);
	i2c_smbus_write_byte_data(client, SI570_REG_FREEZE_DCO,
				  0);
	i2c_smbus_write_byte_data(client, SI570_REG_CONTROL,
				  SI570_CNTRL_NEWFREQ);
	return 0;
}

/*
 * Reset chip.
 * This function must be called with update mutex lock held.
 */
static int si570_reset(struct i2c_client *client, struct si570_data *data)
{
	i2c_smbus_write_byte_data(client, SI570_REG_CONTROL,
				  SI570_CNTRL_RESET);
	usleep_range(1000, 5000);
	return si570_set_frequency(client, data, data->frequency);
}

static ssize_t show_frequency_attr(struct device *dev,
			      struct device_attribute *devattr,
			      char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct si570_data *data = i2c_get_clientdata(client);

	return sprintf(buf, "%llu\n", data->frequency);
}

int get_frequency_si570(struct device *dev, unsigned long *freq)
{
	int err;
	char buf[10+1];

	if ((!dev) || (to_i2c_client(dev) != si570_client))
		return -EINVAL;

	show_frequency_attr(dev, NULL, buf);

	err = kstrtoul(buf, 10, freq);
	if (err)
		return err;

	return 0;
}
EXPORT_SYMBOL(get_frequency_si570);

static ssize_t set_frequency_attr(struct device *dev,
			     struct device_attribute *attr,
			     const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct si570_data *data = i2c_get_clientdata(client);
	unsigned long val;
	int err;

	err = kstrtoul(buf, 10, &val);
	if (err)
		return err;

	if (val < SI570_MIN_FREQ || val > data->max_freq) {
		dev_err(&client->dev,
			"requested frequency %lu Hz is out of range\n", val);
		return -EINVAL;
	}

	mutex_lock(&data->lock);

	if (div64_u64(abs(val - data->frequency) * 10000LL,
		data->frequency) < 35)
		err = si570_set_frequency_small(client, data, val);
	else
		err = si570_set_frequency(client, data, val);
	mutex_unlock(&data->lock);
	if (err) {
		dev_warn(&client->dev,
			"unable to set output frequency %lu Hz: %d\n",
			val, err);
		return err;
	}

	dev_info(&client->dev,
		"set new output frequency %lu Hz\n", val);

	return count;
}

int set_frequency_si570(struct device *dev, unsigned long freq)
{
	char buf[10+1];

	if ((!dev) || (to_i2c_client(dev) != si570_client))
		return -EINVAL;

	sprintf(buf, "%lu", freq);

	return set_frequency_attr(dev, NULL, buf,  0);
}
EXPORT_SYMBOL(set_frequency_si570);

static ssize_t show_reset_attr(struct device *dev,
			  struct device_attribute *devattr,
			  char *buf)
{
	return sprintf(buf, "%d\n", 0);
}

static ssize_t set_reset_attr(struct device *dev,
			 struct device_attribute *attr,
			 const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct si570_data *data = i2c_get_clientdata(client);
	unsigned long val;
	int err;

	err = kstrtoul(buf, 10, &val);
	if (err)
		return err;
	if (val == 0)
		goto done;

	mutex_lock(&data->lock);
	err = si570_reset(client, data);
	mutex_unlock(&data->lock);
	if (err)
		return err;
done:
	return count;
}

int reset_si570(struct device *dev, int id)
{
	char buf[4];

	if ((!dev) || (to_i2c_client(dev) != si570_client))
		return -EINVAL;

	sprintf(buf, "%lu", (unsigned long)id);
	return set_reset_attr(dev, NULL, buf, 0);
}
EXPORT_SYMBOL(reset_si570);

struct i2c_client *get_i2c_client_si570(void)
{
	return si570_client;
}
EXPORT_SYMBOL(get_i2c_client_si570);

static DEVICE_ATTR(frequency, S_IWUSR | S_IRUGO, show_frequency_attr,
		   set_frequency_attr);
static DEVICE_ATTR(reset, S_IWUSR | S_IRUGO, show_reset_attr, set_reset_attr);

static struct attribute *si570_attr[] = {
	&dev_attr_frequency.attr,
	&dev_attr_reset.attr,
	NULL
};

static const struct i2c_device_id si570_id[] = {
	{ "si570", 0 },
	{ "si571", 0 },
	{ "si598", 1 },
	{ "si599", 1 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, si570_id);

static int si570_probe(struct i2c_client *client,
		       const struct i2c_device_id *id)
{
	struct si570_platform_data *pdata = client->dev.platform_data;
	struct si570_data *data;
	int err;
	unsigned long initial_fout;
	u32 tmp = SI570_FOUT_FACTORY_DFLT;

	data = kzalloc(sizeof(struct si570_data), GFP_KERNEL);
	if (!data) {
		err = -ENOMEM;
		goto exit;
	}

	if (id->driver_data) {
		data->fout = SI598_FOUT_FACTORY_DFLT;
		data->max_freq = SI598_MAX_FREQ;
	} else {
		data->fout = SI570_FOUT_FACTORY_DFLT;
		data->max_freq = SI570_MAX_FREQ;
	}

	if (pdata && pdata->factory_fout)
		data->fout = pdata->factory_fout;

	if (client->dev.of_node &&
		(of_property_read_u32(client->dev.of_node, "factory-fout",
			&tmp) < 0))
		dev_warn(&client->dev,
			"DTS does not contain factory-fout, using default\n");
	else
		data->fout = tmp;

	i2c_set_clientdata(client, data);
	err = si570_get_defaults(client);
	if (err < 0)
		goto exit_free;

	mutex_init(&data->lock);

	/* Register sysfs hooks */
	data->attrs.attrs = si570_attr;
	err = sysfs_create_group(&client->dev.kobj, &data->attrs);
	if (err)
		goto exit_free;

	/* Display a message indicating that we've successfully registered */
	dev_info(&client->dev,
		"registered %s with default frequency %llu Hz\n",
		id->name, data->fout);

	/* Read the requested initial fout from either platform data or the
	 * device tree
	 */
	initial_fout = 0;
	if (pdata && pdata->initial_fout)
		initial_fout = pdata->initial_fout;
	if (client->dev.of_node) {
		of_property_read_u32(client->dev.of_node, "initial-fout",
			(u32 *)&initial_fout);
		if (pdata && pdata->initial_fout &&
			(pdata->initial_fout != initial_fout)) {
			dev_warn(&client->dev,
				"OF initial fout %lu overrides platform data fout %lu\n",
				initial_fout,
				pdata->initial_fout);
		}
	}

	if (initial_fout != 0) {
		if (initial_fout < SI570_MIN_FREQ ||
			initial_fout > data->max_freq) {
			dev_err(&client->dev,
				"requested initial frequency %lu is out of range, using default\n",
				initial_fout);
			return 0;
		}

		mutex_lock(&data->lock);

		if (div64_u64(abs(initial_fout - data->frequency) *
			10000LL, data->frequency) < 35)
			err = si570_set_frequency_small(client, data,
				initial_fout);
		else
			err = si570_set_frequency(client, data,
				initial_fout);
		mutex_unlock(&data->lock);
		if (err) {
			dev_warn(&client->dev,
				"unable to set initial output frequency %lu: %d\n",
				initial_fout, err);
			return err;
		}

		dev_info(&client->dev,
			"set initial output frequency %lu Hz\n",
			initial_fout);
	}

	si570_client = client;

	return 0;

exit_free:
	kfree(data);
exit:
	return err;
}

static int si570_remove(struct i2c_client *client)
{
	struct si570_data *data = i2c_get_clientdata(client);

	sysfs_remove_group(&client->dev.kobj, &data->attrs);
	kfree(data);
	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id i2c_si570_of_match[] = {
	{ .compatible = "si570" },
	{ },
};
MODULE_DEVICE_TABLE(of, i2c_si570_of_match);
#endif

static struct i2c_driver si570_driver = {
	.driver = {
		.name	= "si570",
		.of_match_table = of_match_ptr(i2c_si570_of_match),
	},
	.probe		= si570_probe,
	.remove		= si570_remove,
	.id_table	= si570_id,
};

static int __init si570_init(void)
{
	return i2c_add_driver(&si570_driver);
}

static void __exit si570_exit(void)
{
	i2c_del_driver(&si570_driver);
}

MODULE_AUTHOR("Guenter Roeck <guenter.roeck@ericsson.com>");
MODULE_DESCRIPTION("Si570 driver");
MODULE_LICENSE("GPL");

module_init(si570_init);
module_exit(si570_exit);
