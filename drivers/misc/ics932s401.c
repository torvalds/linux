// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * A driver for the Integrated Circuits ICS932S401
 * Copyright (C) 2008 IBM
 *
 * Author: Darrick J. Wong <darrick.wong@oracle.com>
 */

#include <linux/module.h>
#include <linux/jiffies.h>
#include <linux/i2c.h>
#include <linux/err.h>
#include <linux/mutex.h>
#include <linux/delay.h>
#include <linux/log2.h>
#include <linux/slab.h>

/* Addresses to scan */
static const unsigned short normal_i2c[] = { 0x69, I2C_CLIENT_END };

/* ICS932S401 registers */
#define ICS932S401_REG_CFG2			0x01
#define		ICS932S401_CFG1_SPREAD		0x01
#define ICS932S401_REG_CFG7			0x06
#define		ICS932S401_FS_MASK		0x07
#define	ICS932S401_REG_VENDOR_REV		0x07
#define		ICS932S401_VENDOR		1
#define		ICS932S401_VENDOR_MASK		0x0F
#define		ICS932S401_REV			4
#define		ICS932S401_REV_SHIFT		4
#define ICS932S401_REG_DEVICE			0x09
#define		ICS932S401_DEVICE		11
#define	ICS932S401_REG_CTRL			0x0A
#define		ICS932S401_MN_ENABLED		0x80
#define		ICS932S401_CPU_ALT		0x04
#define		ICS932S401_SRC_ALT		0x08
#define ICS932S401_REG_CPU_M_CTRL		0x0B
#define		ICS932S401_M_MASK		0x3F
#define	ICS932S401_REG_CPU_N_CTRL		0x0C
#define	ICS932S401_REG_CPU_SPREAD1		0x0D
#define ICS932S401_REG_CPU_SPREAD2		0x0E
#define		ICS932S401_SPREAD_MASK		0x7FFF
#define ICS932S401_REG_SRC_M_CTRL		0x0F
#define ICS932S401_REG_SRC_N_CTRL		0x10
#define	ICS932S401_REG_SRC_SPREAD1		0x11
#define ICS932S401_REG_SRC_SPREAD2		0x12
#define ICS932S401_REG_CPU_DIVISOR		0x13
#define		ICS932S401_CPU_DIVISOR_SHIFT	4
#define ICS932S401_REG_PCISRC_DIVISOR		0x14
#define		ICS932S401_SRC_DIVISOR_MASK	0x0F
#define		ICS932S401_PCI_DIVISOR_SHIFT	4

/* Base clock is 14.318MHz */
#define BASE_CLOCK				14318

#define NUM_REGS				21
#define NUM_MIRRORED_REGS			15

static int regs_to_copy[NUM_MIRRORED_REGS] = {
	ICS932S401_REG_CFG2,
	ICS932S401_REG_CFG7,
	ICS932S401_REG_VENDOR_REV,
	ICS932S401_REG_DEVICE,
	ICS932S401_REG_CTRL,
	ICS932S401_REG_CPU_M_CTRL,
	ICS932S401_REG_CPU_N_CTRL,
	ICS932S401_REG_CPU_SPREAD1,
	ICS932S401_REG_CPU_SPREAD2,
	ICS932S401_REG_SRC_M_CTRL,
	ICS932S401_REG_SRC_N_CTRL,
	ICS932S401_REG_SRC_SPREAD1,
	ICS932S401_REG_SRC_SPREAD2,
	ICS932S401_REG_CPU_DIVISOR,
	ICS932S401_REG_PCISRC_DIVISOR,
};

/* How often do we reread sensors values? (In jiffies) */
#define SENSOR_REFRESH_INTERVAL	(2 * HZ)

/* How often do we reread sensor limit values? (In jiffies) */
#define LIMIT_REFRESH_INTERVAL	(60 * HZ)

struct ics932s401_data {
	struct attribute_group	attrs;
	struct mutex		lock;
	char			sensors_valid;
	unsigned long		sensors_last_updated;	/* In jiffies */

	u8			regs[NUM_REGS];
};

static int ics932s401_probe(struct i2c_client *client,
			 const struct i2c_device_id *id);
static int ics932s401_detect(struct i2c_client *client,
			  struct i2c_board_info *info);
static int ics932s401_remove(struct i2c_client *client);

static const struct i2c_device_id ics932s401_id[] = {
	{ "ics932s401", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ics932s401_id);

static struct i2c_driver ics932s401_driver = {
	.class		= I2C_CLASS_HWMON,
	.driver = {
		.name	= "ics932s401",
	},
	.probe		= ics932s401_probe,
	.remove		= ics932s401_remove,
	.id_table	= ics932s401_id,
	.detect		= ics932s401_detect,
	.address_list	= normal_i2c,
};

static struct ics932s401_data *ics932s401_update_device(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct ics932s401_data *data = i2c_get_clientdata(client);
	unsigned long local_jiffies = jiffies;
	int i, temp;

	mutex_lock(&data->lock);
	if (time_before(local_jiffies, data->sensors_last_updated +
		SENSOR_REFRESH_INTERVAL)
		&& data->sensors_valid)
		goto out;

	/*
	 * Each register must be read as a word and then right shifted 8 bits.
	 * Not really sure why this is; setting the "byte count programming"
	 * register to 1 does not fix this problem.
	 */
	for (i = 0; i < NUM_MIRRORED_REGS; i++) {
		temp = i2c_smbus_read_word_data(client, regs_to_copy[i]);
		if (temp < 0)
			data->regs[regs_to_copy[i]] = 0;
		data->regs[regs_to_copy[i]] = temp >> 8;
	}

	data->sensors_last_updated = local_jiffies;
	data->sensors_valid = 1;

out:
	mutex_unlock(&data->lock);
	return data;
}

static ssize_t show_spread_enabled(struct device *dev,
				   struct device_attribute *devattr,
				   char *buf)
{
	struct ics932s401_data *data = ics932s401_update_device(dev);

	if (data->regs[ICS932S401_REG_CFG2] & ICS932S401_CFG1_SPREAD)
		return sprintf(buf, "1\n");

	return sprintf(buf, "0\n");
}

/* bit to cpu khz map */
static const int fs_speeds[] = {
	266666,
	133333,
	200000,
	166666,
	333333,
	100000,
	400000,
	0,
};

/* clock divisor map */
static const int divisors[] = {2, 3, 5, 15, 4, 6, 10, 30, 8, 12, 20, 60, 16,
			       24, 40, 120};

/* Calculate CPU frequency from the M/N registers. */
static int calculate_cpu_freq(struct ics932s401_data *data)
{
	int m, n, freq;

	m = data->regs[ICS932S401_REG_CPU_M_CTRL] & ICS932S401_M_MASK;
	n = data->regs[ICS932S401_REG_CPU_N_CTRL];

	/* Pull in bits 8 & 9 from the M register */
	n |= ((int)data->regs[ICS932S401_REG_CPU_M_CTRL] & 0x80) << 1;
	n |= ((int)data->regs[ICS932S401_REG_CPU_M_CTRL] & 0x40) << 3;

	freq = BASE_CLOCK * (n + 8) / (m + 2);
	freq /= divisors[data->regs[ICS932S401_REG_CPU_DIVISOR] >>
			 ICS932S401_CPU_DIVISOR_SHIFT];

	return freq;
}

static ssize_t show_cpu_clock(struct device *dev,
			      struct device_attribute *devattr,
			      char *buf)
{
	struct ics932s401_data *data = ics932s401_update_device(dev);

	return sprintf(buf, "%d\n", calculate_cpu_freq(data));
}

static ssize_t show_cpu_clock_sel(struct device *dev,
				  struct device_attribute *devattr,
				  char *buf)
{
	struct ics932s401_data *data = ics932s401_update_device(dev);
	int freq;

	if (data->regs[ICS932S401_REG_CTRL] & ICS932S401_MN_ENABLED)
		freq = calculate_cpu_freq(data);
	else {
		/* Freq is neatly wrapped up for us */
		int fid = data->regs[ICS932S401_REG_CFG7] & ICS932S401_FS_MASK;

		freq = fs_speeds[fid];
		if (data->regs[ICS932S401_REG_CTRL] & ICS932S401_CPU_ALT) {
			switch (freq) {
			case 166666:
				freq = 160000;
				break;
			case 333333:
				freq = 320000;
				break;
			}
		}
	}

	return sprintf(buf, "%d\n", freq);
}

/* Calculate SRC frequency from the M/N registers. */
static int calculate_src_freq(struct ics932s401_data *data)
{
	int m, n, freq;

	m = data->regs[ICS932S401_REG_SRC_M_CTRL] & ICS932S401_M_MASK;
	n = data->regs[ICS932S401_REG_SRC_N_CTRL];

	/* Pull in bits 8 & 9 from the M register */
	n |= ((int)data->regs[ICS932S401_REG_SRC_M_CTRL] & 0x80) << 1;
	n |= ((int)data->regs[ICS932S401_REG_SRC_M_CTRL] & 0x40) << 3;

	freq = BASE_CLOCK * (n + 8) / (m + 2);
	freq /= divisors[data->regs[ICS932S401_REG_PCISRC_DIVISOR] &
			 ICS932S401_SRC_DIVISOR_MASK];

	return freq;
}

static ssize_t show_src_clock(struct device *dev,
			      struct device_attribute *devattr,
			      char *buf)
{
	struct ics932s401_data *data = ics932s401_update_device(dev);

	return sprintf(buf, "%d\n", calculate_src_freq(data));
}

static ssize_t show_src_clock_sel(struct device *dev,
				  struct device_attribute *devattr,
				  char *buf)
{
	struct ics932s401_data *data = ics932s401_update_device(dev);
	int freq;

	if (data->regs[ICS932S401_REG_CTRL] & ICS932S401_MN_ENABLED)
		freq = calculate_src_freq(data);
	else
		/* Freq is neatly wrapped up for us */
		if (data->regs[ICS932S401_REG_CTRL] & ICS932S401_CPU_ALT &&
		    data->regs[ICS932S401_REG_CTRL] & ICS932S401_SRC_ALT)
			freq = 96000;
		else
			freq = 100000;

	return sprintf(buf, "%d\n", freq);
}

/* Calculate PCI frequency from the SRC M/N registers. */
static int calculate_pci_freq(struct ics932s401_data *data)
{
	int m, n, freq;

	m = data->regs[ICS932S401_REG_SRC_M_CTRL] & ICS932S401_M_MASK;
	n = data->regs[ICS932S401_REG_SRC_N_CTRL];

	/* Pull in bits 8 & 9 from the M register */
	n |= ((int)data->regs[ICS932S401_REG_SRC_M_CTRL] & 0x80) << 1;
	n |= ((int)data->regs[ICS932S401_REG_SRC_M_CTRL] & 0x40) << 3;

	freq = BASE_CLOCK * (n + 8) / (m + 2);
	freq /= divisors[data->regs[ICS932S401_REG_PCISRC_DIVISOR] >>
			 ICS932S401_PCI_DIVISOR_SHIFT];

	return freq;
}

static ssize_t show_pci_clock(struct device *dev,
			      struct device_attribute *devattr,
			      char *buf)
{
	struct ics932s401_data *data = ics932s401_update_device(dev);

	return sprintf(buf, "%d\n", calculate_pci_freq(data));
}

static ssize_t show_pci_clock_sel(struct device *dev,
				  struct device_attribute *devattr,
				  char *buf)
{
	struct ics932s401_data *data = ics932s401_update_device(dev);
	int freq;

	if (data->regs[ICS932S401_REG_CTRL] & ICS932S401_MN_ENABLED)
		freq = calculate_pci_freq(data);
	else
		freq = 33333;

	return sprintf(buf, "%d\n", freq);
}

static ssize_t show_value(struct device *dev,
			  struct device_attribute *devattr,
			  char *buf);

static ssize_t show_spread(struct device *dev,
			   struct device_attribute *devattr,
			   char *buf);

static DEVICE_ATTR(spread_enabled, S_IRUGO, show_spread_enabled, NULL);
static DEVICE_ATTR(cpu_clock_selection, S_IRUGO, show_cpu_clock_sel, NULL);
static DEVICE_ATTR(cpu_clock, S_IRUGO, show_cpu_clock, NULL);
static DEVICE_ATTR(src_clock_selection, S_IRUGO, show_src_clock_sel, NULL);
static DEVICE_ATTR(src_clock, S_IRUGO, show_src_clock, NULL);
static DEVICE_ATTR(pci_clock_selection, S_IRUGO, show_pci_clock_sel, NULL);
static DEVICE_ATTR(pci_clock, S_IRUGO, show_pci_clock, NULL);
static DEVICE_ATTR(usb_clock, S_IRUGO, show_value, NULL);
static DEVICE_ATTR(ref_clock, S_IRUGO, show_value, NULL);
static DEVICE_ATTR(cpu_spread, S_IRUGO, show_spread, NULL);
static DEVICE_ATTR(src_spread, S_IRUGO, show_spread, NULL);

static struct attribute *ics932s401_attr[] = {
	&dev_attr_spread_enabled.attr,
	&dev_attr_cpu_clock_selection.attr,
	&dev_attr_cpu_clock.attr,
	&dev_attr_src_clock_selection.attr,
	&dev_attr_src_clock.attr,
	&dev_attr_pci_clock_selection.attr,
	&dev_attr_pci_clock.attr,
	&dev_attr_usb_clock.attr,
	&dev_attr_ref_clock.attr,
	&dev_attr_cpu_spread.attr,
	&dev_attr_src_spread.attr,
	NULL
};

static ssize_t show_value(struct device *dev,
			  struct device_attribute *devattr,
			  char *buf)
{
	int x;

	if (devattr == &dev_attr_usb_clock)
		x = 48000;
	else if (devattr == &dev_attr_ref_clock)
		x = BASE_CLOCK;
	else
		BUG();

	return sprintf(buf, "%d\n", x);
}

static ssize_t show_spread(struct device *dev,
			   struct device_attribute *devattr,
			   char *buf)
{
	struct ics932s401_data *data = ics932s401_update_device(dev);
	int reg;
	unsigned long val;

	if (!(data->regs[ICS932S401_REG_CFG2] & ICS932S401_CFG1_SPREAD))
		return sprintf(buf, "0%%\n");

	if (devattr == &dev_attr_src_spread)
		reg = ICS932S401_REG_SRC_SPREAD1;
	else if (devattr == &dev_attr_cpu_spread)
		reg = ICS932S401_REG_CPU_SPREAD1;
	else
		BUG();

	val = data->regs[reg] | (data->regs[reg + 1] << 8);
	val &= ICS932S401_SPREAD_MASK;

	/* Scale 0..2^14 to -0.5. */
	val = 500000 * val / 16384;
	return sprintf(buf, "-0.%lu%%\n", val);
}

/* Return 0 if detection is successful, -ENODEV otherwise */
static int ics932s401_detect(struct i2c_client *client,
			  struct i2c_board_info *info)
{
	struct i2c_adapter *adapter = client->adapter;
	int vendor, device, revision;

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA))
		return -ENODEV;

	vendor = i2c_smbus_read_word_data(client, ICS932S401_REG_VENDOR_REV);
	vendor >>= 8;
	revision = vendor >> ICS932S401_REV_SHIFT;
	vendor &= ICS932S401_VENDOR_MASK;
	if (vendor != ICS932S401_VENDOR)
		return -ENODEV;

	device = i2c_smbus_read_word_data(client, ICS932S401_REG_DEVICE);
	device >>= 8;
	if (device != ICS932S401_DEVICE)
		return -ENODEV;

	if (revision != ICS932S401_REV)
		dev_info(&adapter->dev, "Unknown revision %d\n", revision);

	strlcpy(info->type, "ics932s401", I2C_NAME_SIZE);

	return 0;
}

static int ics932s401_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct ics932s401_data *data;
	int err;

	data = kzalloc(sizeof(struct ics932s401_data), GFP_KERNEL);
	if (!data) {
		err = -ENOMEM;
		goto exit;
	}

	i2c_set_clientdata(client, data);
	mutex_init(&data->lock);

	dev_info(&client->dev, "%s chip found\n", client->name);

	/* Register sysfs hooks */
	data->attrs.attrs = ics932s401_attr;
	err = sysfs_create_group(&client->dev.kobj, &data->attrs);
	if (err)
		goto exit_free;

	return 0;

exit_free:
	kfree(data);
exit:
	return err;
}

static int ics932s401_remove(struct i2c_client *client)
{
	struct ics932s401_data *data = i2c_get_clientdata(client);

	sysfs_remove_group(&client->dev.kobj, &data->attrs);
	kfree(data);
	return 0;
}

module_i2c_driver(ics932s401_driver);

MODULE_AUTHOR("Darrick J. Wong <darrick.wong@oracle.com>");
MODULE_DESCRIPTION("ICS932S401 driver");
MODULE_LICENSE("GPL");

/* IBM IntelliStation Z30 */
MODULE_ALIAS("dmi:bvnIBM:*:rn9228:*");
MODULE_ALIAS("dmi:bvnIBM:*:rn9232:*");

/* IBM x3650/x3550 */
MODULE_ALIAS("dmi:bvnIBM:*:pnIBMSystemx3650*");
MODULE_ALIAS("dmi:bvnIBM:*:pnIBMSystemx3550*");
