/*
 * k8temp.c - Linux kernel module for hardware monitoring
 *
 * Copyright (C) 2006 Rudolf Marek <r.marek@assembler.cz>
 *
 * Inspired from the w83785 and amd756 drivers.
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
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA.
 */

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/jiffies.h>
#include <linux/pci.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/err.h>
#include <linux/mutex.h>
#include <asm/processor.h>

#define TEMP_FROM_REG(val)	(((((val) >> 16) & 0xff) - 49) * 1000)
#define REG_TEMP	0xe4
#define SEL_PLACE	0x40
#define SEL_CORE	0x04

struct k8temp_data {
	struct device *hwmon_dev;
	struct mutex update_lock;
	const char *name;
	char valid;		/* zero until following fields are valid */
	unsigned long last_updated;	/* in jiffies */

	/* registers values */
	u8 sensorsp;		/* sensor presence bits - SEL_CORE & SEL_PLACE */
	u32 temp[2][2];		/* core, place */
	u8 swap_core_select;    /* meaning of SEL_CORE is inverted */
	u32 temp_offset;
};

static struct k8temp_data *k8temp_update_device(struct device *dev)
{
	struct k8temp_data *data = dev_get_drvdata(dev);
	struct pci_dev *pdev = to_pci_dev(dev);
	u8 tmp;

	mutex_lock(&data->update_lock);

	if (!data->valid
	    || time_after(jiffies, data->last_updated + HZ)) {
		pci_read_config_byte(pdev, REG_TEMP, &tmp);
		tmp &= ~(SEL_PLACE | SEL_CORE);		/* Select sensor 0, core0 */
		pci_write_config_byte(pdev, REG_TEMP, tmp);
		pci_read_config_dword(pdev, REG_TEMP, &data->temp[0][0]);

		if (data->sensorsp & SEL_PLACE) {
			tmp |= SEL_PLACE;	/* Select sensor 1, core0 */
			pci_write_config_byte(pdev, REG_TEMP, tmp);
			pci_read_config_dword(pdev, REG_TEMP,
					      &data->temp[0][1]);
		}

		if (data->sensorsp & SEL_CORE) {
			tmp &= ~SEL_PLACE;	/* Select sensor 0, core1 */
			tmp |= SEL_CORE;
			pci_write_config_byte(pdev, REG_TEMP, tmp);
			pci_read_config_dword(pdev, REG_TEMP,
					      &data->temp[1][0]);

			if (data->sensorsp & SEL_PLACE) {
				tmp |= SEL_PLACE;	/* Select sensor 1, core1 */
				pci_write_config_byte(pdev, REG_TEMP, tmp);
				pci_read_config_dword(pdev, REG_TEMP,
						      &data->temp[1][1]);
			}
		}

		data->last_updated = jiffies;
		data->valid = 1;
	}

	mutex_unlock(&data->update_lock);
	return data;
}

/*
 * Sysfs stuff
 */

static ssize_t show_name(struct device *dev, struct device_attribute
			 *devattr, char *buf)
{
	struct k8temp_data *data = dev_get_drvdata(dev);

	return sprintf(buf, "%s\n", data->name);
}


static ssize_t show_temp(struct device *dev,
			 struct device_attribute *devattr, char *buf)
{
	struct sensor_device_attribute_2 *attr =
	    to_sensor_dev_attr_2(devattr);
	int core = attr->nr;
	int place = attr->index;
	int temp;
	struct k8temp_data *data = k8temp_update_device(dev);

	if (data->swap_core_select && (data->sensorsp & SEL_CORE))
		core = core ? 0 : 1;

	temp = TEMP_FROM_REG(data->temp[core][place]) + data->temp_offset;

	return sprintf(buf, "%d\n", temp);
}

/* core, place */

static SENSOR_DEVICE_ATTR_2(temp1_input, S_IRUGO, show_temp, NULL, 0, 0);
static SENSOR_DEVICE_ATTR_2(temp2_input, S_IRUGO, show_temp, NULL, 0, 1);
static SENSOR_DEVICE_ATTR_2(temp3_input, S_IRUGO, show_temp, NULL, 1, 0);
static SENSOR_DEVICE_ATTR_2(temp4_input, S_IRUGO, show_temp, NULL, 1, 1);
static DEVICE_ATTR(name, S_IRUGO, show_name, NULL);

static const struct pci_device_id k8temp_ids[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_AMD, PCI_DEVICE_ID_AMD_K8_NB_MISC) },
	{ 0 },
};

MODULE_DEVICE_TABLE(pci, k8temp_ids);

static int __devinit k8temp_probe(struct pci_dev *pdev,
				  const struct pci_device_id *id)
{
	int err;
	u8 scfg;
	u32 temp;
	u8 model, stepping;
	struct k8temp_data *data;

	if (!(data = kzalloc(sizeof(struct k8temp_data), GFP_KERNEL))) {
		err = -ENOMEM;
		goto exit;
	}

	model = boot_cpu_data.x86_model;
	stepping = boot_cpu_data.x86_mask;

	switch (boot_cpu_data.x86) {
	case 0xf:
		/* feature available since SH-C0, exclude older revisions */
		if (((model == 4) && (stepping == 0)) ||
		    ((model == 5) && (stepping <= 1))) {
			err = -ENODEV;
			goto exit_free;
		}

		/*
		 * AMD NPT family 0fh, i.e. RevF and RevG:
		 * meaning of SEL_CORE bit is inverted
		 */
		if (model >= 0x40) {
			data->swap_core_select = 1;
			dev_warn(&pdev->dev, "Temperature readouts might be "
				 "wrong - check erratum #141\n");
		}

		if ((model >= 0x69) &&
		    !(model == 0xc1 || model == 0x6c || model == 0x7c ||
		      model == 0x6b || model == 0x6f || model == 0x7f)) {
			/*
			 * RevG desktop CPUs (i.e. no socket S1G1 or
			 * ASB1 parts) need additional offset,
			 * otherwise reported temperature is below
			 * ambient temperature
			 */
			data->temp_offset = 21000;
		}

		break;
	}

	pci_read_config_byte(pdev, REG_TEMP, &scfg);
	scfg &= ~(SEL_PLACE | SEL_CORE);		/* Select sensor 0, core0 */
	pci_write_config_byte(pdev, REG_TEMP, scfg);
	pci_read_config_byte(pdev, REG_TEMP, &scfg);

	if (scfg & (SEL_PLACE | SEL_CORE)) {
		dev_err(&pdev->dev, "Configuration bit(s) stuck at 1!\n");
		err = -ENODEV;
		goto exit_free;
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
		if (!((temp >> 16) & 0xff))	/* if temp is 0 -49C is not likely */
			data->sensorsp &= ~SEL_PLACE;
	}

	if (data->sensorsp & SEL_CORE) {
		scfg &= ~SEL_PLACE;	/* Select sensor 0, core1 */
		pci_write_config_byte(pdev, REG_TEMP, scfg);
		pci_read_config_dword(pdev, REG_TEMP, &temp);
		if (!((temp >> 16) & 0xff))	/* if temp is 0 -49C is not likely */
			data->sensorsp &= ~SEL_CORE;
	}

	data->name = "k8temp";
	mutex_init(&data->update_lock);
	dev_set_drvdata(&pdev->dev, data);

	/* Register sysfs hooks */
	err = device_create_file(&pdev->dev,
			   &sensor_dev_attr_temp1_input.dev_attr);
	if (err)
		goto exit_remove;

	/* sensor can be changed and reports something */
	if (data->sensorsp & SEL_PLACE) {
		err = device_create_file(&pdev->dev,
				   &sensor_dev_attr_temp2_input.dev_attr);
		if (err)
			goto exit_remove;
	}

	/* core can be changed and reports something */
	if (data->sensorsp & SEL_CORE) {
		err = device_create_file(&pdev->dev,
				   &sensor_dev_attr_temp3_input.dev_attr);
		if (err)
			goto exit_remove;
		if (data->sensorsp & SEL_PLACE)
			err = device_create_file(&pdev->dev,
					   &sensor_dev_attr_temp4_input.
					   dev_attr);
			if (err)
				goto exit_remove;
	}

	err = device_create_file(&pdev->dev, &dev_attr_name);
	if (err)
		goto exit_remove;

	data->hwmon_dev = hwmon_device_register(&pdev->dev);

	if (IS_ERR(data->hwmon_dev)) {
		err = PTR_ERR(data->hwmon_dev);
		goto exit_remove;
	}

	return 0;

exit_remove:
	device_remove_file(&pdev->dev,
			   &sensor_dev_attr_temp1_input.dev_attr);
	device_remove_file(&pdev->dev,
			   &sensor_dev_attr_temp2_input.dev_attr);
	device_remove_file(&pdev->dev,
			   &sensor_dev_attr_temp3_input.dev_attr);
	device_remove_file(&pdev->dev,
			   &sensor_dev_attr_temp4_input.dev_attr);
	device_remove_file(&pdev->dev, &dev_attr_name);
exit_free:
	dev_set_drvdata(&pdev->dev, NULL);
	kfree(data);
exit:
	return err;
}

static void __devexit k8temp_remove(struct pci_dev *pdev)
{
	struct k8temp_data *data = dev_get_drvdata(&pdev->dev);

	hwmon_device_unregister(data->hwmon_dev);
	device_remove_file(&pdev->dev,
			   &sensor_dev_attr_temp1_input.dev_attr);
	device_remove_file(&pdev->dev,
			   &sensor_dev_attr_temp2_input.dev_attr);
	device_remove_file(&pdev->dev,
			   &sensor_dev_attr_temp3_input.dev_attr);
	device_remove_file(&pdev->dev,
			   &sensor_dev_attr_temp4_input.dev_attr);
	device_remove_file(&pdev->dev, &dev_attr_name);
	dev_set_drvdata(&pdev->dev, NULL);
	kfree(data);
}

static struct pci_driver k8temp_driver = {
	.name = "k8temp",
	.id_table = k8temp_ids,
	.probe = k8temp_probe,
	.remove = __devexit_p(k8temp_remove),
};

static int __init k8temp_init(void)
{
	return pci_register_driver(&k8temp_driver);
}

static void __exit k8temp_exit(void)
{
	pci_unregister_driver(&k8temp_driver);
}

MODULE_AUTHOR("Rudolf Marek <r.marek@assembler.cz>");
MODULE_DESCRIPTION("AMD K8 core temperature monitor");
MODULE_LICENSE("GPL");

module_init(k8temp_init)
module_exit(k8temp_exit)
