// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * k8temp.c - Linux kernel module for hardware monitoring
 *
 * Copyright (C) 2006 Rudolf Marek <r.marek@assembler.cz>
 *
 * Inspired from the w83785 and amd756 drivers.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/pci.h>
#include <linux/hwmon.h>
#include <linux/err.h>
#include <linux/mutex.h>
#include <asm/processor.h>

#define TEMP_FROM_REG(val)	(((((val) >> 16) & 0xff) - 49) * 1000)
#define REG_TEMP	0xe4
#define SEL_PLACE	0x40
#define SEL_CORE	0x04

struct k8temp_data {
	struct mutex update_lock;

	/* registers values */
	u8 sensorsp;		/* sensor presence bits - SEL_CORE, SEL_PLACE */
	u8 swap_core_select;    /* meaning of SEL_CORE is inverted */
	u32 temp_offset;
};

static const struct pci_device_id k8temp_ids[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_AMD, PCI_DEVICE_ID_AMD_K8_NB_MISC) },
	{ 0 },
};
MODULE_DEVICE_TABLE(pci, k8temp_ids);

static int is_rev_g_desktop(u8 model)
{
	u32 brandidx;

	if (model < 0x69)
		return 0;

	if (model == 0xc1 || model == 0x6c || model == 0x7c)
		return 0;

	/*
	 * Differentiate between AM2 and ASB1.
	 * See "Constructing the processor Name String" in "Revision
	 * Guide for AMD NPT Family 0Fh Processors" (33610).
	 */
	brandidx = cpuid_ebx(0x80000001);
	brandidx = (brandidx >> 9) & 0x1f;

	/* Single core */
	if ((model == 0x6f || model == 0x7f) &&
	    (brandidx == 0x7 || brandidx == 0x9 || brandidx == 0xc))
		return 0;

	/* Dual core */
	if (model == 0x6b &&
	    (brandidx == 0xb || brandidx == 0xc))
		return 0;

	return 1;
}

static umode_t
k8temp_is_visible(const void *drvdata, enum hwmon_sensor_types type,
		  u32 attr, int channel)
{
	const struct k8temp_data *data = drvdata;

	if ((channel & 1) && !(data->sensorsp & SEL_PLACE))
		return 0;

	if ((channel & 2) && !(data->sensorsp & SEL_CORE))
		return 0;

	return 0444;
}

static int
k8temp_read(struct device *dev, enum hwmon_sensor_types type,
	    u32 attr, int channel, long *val)
{
	struct k8temp_data *data = dev_get_drvdata(dev);
	struct pci_dev *pdev = to_pci_dev(dev->parent);
	int core, place;
	u32 temp;
	u8 tmp;

	core = (channel >> 1) & 1;
	place = channel & 1;

	core ^= data->swap_core_select;

	mutex_lock(&data->update_lock);
	pci_read_config_byte(pdev, REG_TEMP, &tmp);
	tmp &= ~(SEL_PLACE | SEL_CORE);
	if (core)
		tmp |= SEL_CORE;
	if (place)
		tmp |= SEL_PLACE;
	pci_write_config_byte(pdev, REG_TEMP, tmp);
	pci_read_config_dword(pdev, REG_TEMP, &temp);
	mutex_unlock(&data->update_lock);

	*val = TEMP_FROM_REG(temp) + data->temp_offset;

	return 0;
}

static const struct hwmon_ops k8temp_ops = {
	.is_visible = k8temp_is_visible,
	.read = k8temp_read,
};

static const struct hwmon_channel_info *k8temp_info[] = {
	HWMON_CHANNEL_INFO(temp,
		HWMON_T_INPUT, HWMON_T_INPUT, HWMON_T_INPUT, HWMON_T_INPUT),
	NULL
};

static const struct hwmon_chip_info k8temp_chip_info = {
	.ops = &k8temp_ops,
	.info = k8temp_info,
};

static int k8temp_probe(struct pci_dev *pdev,
				  const struct pci_device_id *id)
{
	u8 scfg;
	u32 temp;
	u8 model, stepping;
	struct k8temp_data *data;
	struct device *hwmon_dev;

	data = devm_kzalloc(&pdev->dev, sizeof(struct k8temp_data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	model = boot_cpu_data.x86_model;
	stepping = boot_cpu_data.x86_stepping;

	/* feature available since SH-C0, exclude older revisions */
	if ((model == 4 && stepping == 0) ||
	    (model == 5 && stepping <= 1))
		return -ENODEV;

	/*
	 * AMD NPT family 0fh, i.e. RevF and RevG:
	 * meaning of SEL_CORE bit is inverted
	 */
	if (model >= 0x40) {
		data->swap_core_select = 1;
		dev_warn(&pdev->dev,
			 "Temperature readouts might be wrong - check erratum #141\n");
	}

	/*
	 * RevG desktop CPUs (i.e. no socket S1G1 or ASB1 parts) need
	 * additional offset, otherwise reported temperature is below
	 * ambient temperature
	 */
	if (is_rev_g_desktop(model))
		data->temp_offset = 21000;

	pci_read_config_byte(pdev, REG_TEMP, &scfg);
	scfg &= ~(SEL_PLACE | SEL_CORE);	/* Select sensor 0, core0 */
	pci_write_config_byte(pdev, REG_TEMP, scfg);
	pci_read_config_byte(pdev, REG_TEMP, &scfg);

	if (scfg & (SEL_PLACE | SEL_CORE)) {
		dev_err(&pdev->dev, "Configuration bit(s) stuck at 1!\n");
		return -ENODEV;
	}

	scfg |= (SEL_PLACE | SEL_CORE);
	pci_write_config_byte(pdev, REG_TEMP, scfg);

	/* now we know if we can change core and/or sensor */
	pci_read_config_byte(pdev, REG_TEMP, &data->sensorsp);

	if (data->sensorsp & SEL_PLACE) {
		scfg &= ~SEL_CORE;	/* Select sensor 1, core0 */
		pci_write_config_byte(pdev, REG_TEMP, scfg);
		pci_read_config_dword(pdev, REG_TEMP, &temp);
		scfg |= SEL_CORE;	/* prepare for next selection */
		if (!((temp >> 16) & 0xff)) /* if temp is 0 -49C is unlikely */
			data->sensorsp &= ~SEL_PLACE;
	}

	if (data->sensorsp & SEL_CORE) {
		scfg &= ~SEL_PLACE;	/* Select sensor 0, core1 */
		pci_write_config_byte(pdev, REG_TEMP, scfg);
		pci_read_config_dword(pdev, REG_TEMP, &temp);
		if (!((temp >> 16) & 0xff)) /* if temp is 0 -49C is unlikely */
			data->sensorsp &= ~SEL_CORE;
	}

	mutex_init(&data->update_lock);

	hwmon_dev = devm_hwmon_device_register_with_info(&pdev->dev,
							 "k8temp",
							 data,
							 &k8temp_chip_info,
							 NULL);

	return PTR_ERR_OR_ZERO(hwmon_dev);
}

static struct pci_driver k8temp_driver = {
	.name = "k8temp",
	.id_table = k8temp_ids,
	.probe = k8temp_probe,
};

module_pci_driver(k8temp_driver);

MODULE_AUTHOR("Rudolf Marek <r.marek@assembler.cz>");
MODULE_DESCRIPTION("AMD K8 core temperature monitor");
MODULE_LICENSE("GPL");
