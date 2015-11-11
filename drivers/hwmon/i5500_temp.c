/*
 * i5500_temp - Driver for Intel 5500/5520/X58 chipset thermal sensor
 *
 * Copyright (C) 2012, 2014 Jean Delvare <jdelvare@suse.de>
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
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/jiffies.h>
#include <linux/device.h>
#include <linux/pci.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/err.h>
#include <linux/mutex.h>

/* Register definitions from datasheet */
#define REG_TSTHRCATA	0xE2
#define REG_TSCTRL	0xE8
#define REG_TSTHRRPEX	0xEB
#define REG_TSTHRLO	0xEC
#define REG_TSTHRHI	0xEE
#define REG_CTHINT	0xF0
#define REG_TSFSC	0xF3
#define REG_CTSTS	0xF4
#define REG_TSTHRRQPI	0xF5
#define REG_CTCTRL	0xF7
#define REG_TSTIMER	0xF8

/*
 * Sysfs stuff
 */

/* Sensor resolution : 0.5 degree C */
static ssize_t show_temp(struct device *dev,
			 struct device_attribute *devattr, char *buf)
{
	struct pci_dev *pdev = to_pci_dev(dev->parent);
	long temp;
	u16 tsthrhi;
	s8 tsfsc;

	pci_read_config_word(pdev, REG_TSTHRHI, &tsthrhi);
	pci_read_config_byte(pdev, REG_TSFSC, &tsfsc);
	temp = ((long)tsthrhi - tsfsc) * 500;

	return sprintf(buf, "%ld\n", temp);
}

static ssize_t show_thresh(struct device *dev,
			   struct device_attribute *devattr, char *buf)
{
	struct pci_dev *pdev = to_pci_dev(dev->parent);
	int reg = to_sensor_dev_attr(devattr)->index;
	long temp;
	u16 tsthr;

	pci_read_config_word(pdev, reg, &tsthr);
	temp = tsthr * 500;

	return sprintf(buf, "%ld\n", temp);
}

static ssize_t show_alarm(struct device *dev,
			  struct device_attribute *devattr, char *buf)
{
	struct pci_dev *pdev = to_pci_dev(dev->parent);
	int nr = to_sensor_dev_attr(devattr)->index;
	u8 ctsts;

	pci_read_config_byte(pdev, REG_CTSTS, &ctsts);
	return sprintf(buf, "%u\n", (unsigned int)ctsts & (1 << nr));
}

static DEVICE_ATTR(temp1_input, S_IRUGO, show_temp, NULL);
static SENSOR_DEVICE_ATTR(temp1_crit, S_IRUGO, show_thresh, NULL, 0xE2);
static SENSOR_DEVICE_ATTR(temp1_max_hyst, S_IRUGO, show_thresh, NULL, 0xEC);
static SENSOR_DEVICE_ATTR(temp1_max, S_IRUGO, show_thresh, NULL, 0xEE);
static SENSOR_DEVICE_ATTR(temp1_crit_alarm, S_IRUGO, show_alarm, NULL, 0);
static SENSOR_DEVICE_ATTR(temp1_max_alarm, S_IRUGO, show_alarm, NULL, 1);

static struct attribute *i5500_temp_attrs[] = {
	&dev_attr_temp1_input.attr,
	&sensor_dev_attr_temp1_crit.dev_attr.attr,
	&sensor_dev_attr_temp1_max_hyst.dev_attr.attr,
	&sensor_dev_attr_temp1_max.dev_attr.attr,
	&sensor_dev_attr_temp1_crit_alarm.dev_attr.attr,
	&sensor_dev_attr_temp1_max_alarm.dev_attr.attr,
	NULL
};

ATTRIBUTE_GROUPS(i5500_temp);

static const struct pci_device_id i5500_temp_ids[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x3438) },
	{ 0 },
};

MODULE_DEVICE_TABLE(pci, i5500_temp_ids);

static int i5500_temp_probe(struct pci_dev *pdev,
			    const struct pci_device_id *id)
{
	int err;
	struct device *hwmon_dev;
	u32 tstimer;
	s8 tsfsc;

	err = pci_enable_device(pdev);
	if (err) {
		dev_err(&pdev->dev, "Failed to enable device\n");
		return err;
	}

	pci_read_config_byte(pdev, REG_TSFSC, &tsfsc);
	pci_read_config_dword(pdev, REG_TSTIMER, &tstimer);
	if (tsfsc == 0x7F && tstimer == 0x07D30D40) {
		dev_notice(&pdev->dev, "Sensor seems to be disabled\n");
		return -ENODEV;
	}

	hwmon_dev = devm_hwmon_device_register_with_groups(&pdev->dev,
							   "intel5500", NULL,
							   i5500_temp_groups);
	return PTR_ERR_OR_ZERO(hwmon_dev);
}

static struct pci_driver i5500_temp_driver = {
	.name = "i5500_temp",
	.id_table = i5500_temp_ids,
	.probe = i5500_temp_probe,
};

module_pci_driver(i5500_temp_driver);

MODULE_AUTHOR("Jean Delvare <jdelvare@suse.de>");
MODULE_DESCRIPTION("Intel 5500/5520/X58 chipset thermal sensor driver");
MODULE_LICENSE("GPL");
