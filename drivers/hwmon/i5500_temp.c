// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * i5500_temp - Driver for Intel 5500/5520/X58 chipset thermal sensor
 *
 * Copyright (C) 2012, 2014 Jean Delvare <jdelvare@suse.de>
 */

#include <linux/bitops.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/jiffies.h>
#include <linux/device.h>
#include <linux/pci.h>
#include <linux/hwmon.h>
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

static umode_t i5500_is_visible(const void *drvdata, enum hwmon_sensor_types type, u32 attr,
				int channel)
{
	return 0444;
}

static int i5500_read(struct device *dev, enum hwmon_sensor_types type, u32 attr, int channel,
		      long *val)
{
	struct pci_dev *pdev = to_pci_dev(dev->parent);
	u16 tsthr;
	s8 tsfsc;
	u8 ctsts;

	switch (type) {
	case hwmon_temp:
		switch (attr) {
		/* Sensor resolution : 0.5 degree C */
		case hwmon_temp_input:
			pci_read_config_word(pdev, REG_TSTHRHI, &tsthr);
			pci_read_config_byte(pdev, REG_TSFSC, &tsfsc);
			*val = (tsthr - tsfsc) * 500;
			return 0;
		case hwmon_temp_max:
			pci_read_config_word(pdev, REG_TSTHRHI, &tsthr);
			*val = tsthr * 500;
			return 0;
		case hwmon_temp_max_hyst:
			pci_read_config_word(pdev, REG_TSTHRLO, &tsthr);
			*val = tsthr * 500;
			return 0;
		case hwmon_temp_crit:
			pci_read_config_word(pdev, REG_TSTHRCATA, &tsthr);
			*val = tsthr * 500;
			return 0;
		case hwmon_temp_max_alarm:
			pci_read_config_byte(pdev, REG_CTSTS, &ctsts);
			*val = !!(ctsts & BIT(1));
			return 0;
		case hwmon_temp_crit_alarm:
			pci_read_config_byte(pdev, REG_CTSTS, &ctsts);
			*val = !!(ctsts & BIT(0));
			return 0;
		default:
			break;
		}
		break;
	default:
		break;
	}

	return -EOPNOTSUPP;
}

static const struct hwmon_ops i5500_ops = {
	.is_visible = i5500_is_visible,
	.read = i5500_read,
};

static const struct hwmon_channel_info *i5500_info[] = {
	HWMON_CHANNEL_INFO(chip, HWMON_C_REGISTER_TZ),
	HWMON_CHANNEL_INFO(temp,
			   HWMON_T_INPUT | HWMON_T_MAX | HWMON_T_MAX_HYST | HWMON_T_CRIT |
			   HWMON_T_MAX_ALARM | HWMON_T_CRIT_ALARM
			   ),
	NULL
};

static const struct hwmon_chip_info i5500_chip_info = {
	.ops = &i5500_ops,
	.info = i5500_info,
};

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

	err = pcim_enable_device(pdev);
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

	hwmon_dev = devm_hwmon_device_register_with_info(&pdev->dev, "intel5500", NULL,
							 &i5500_chip_info, NULL);
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
