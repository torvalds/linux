/* NXP PCF50633 Power Management Unit (PMU) driver
 *
 * (C) 2006-2008 by Openmoko, Inc.
 * Author: Harald Welte <laforge@openmoko.org>
 * 	   Balaji Rao <balajirrao@openmoko.org>
 * All rights reserved.
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 */

#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/sysfs.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/workqueue.h>
#include <linux/platform_device.h>
#include <linux/i2c.h>
#include <linux/pm.h>
#include <linux/slab.h>
#include <linux/regmap.h>
#include <linux/err.h>

#include <linux/mfd/pcf50633/core.h>

/* Read a block of up to 32 regs  */
int pcf50633_read_block(struct pcf50633 *pcf, u8 reg,
					int nr_regs, u8 *data)
{
	int ret;

	ret = regmap_raw_read(pcf->regmap, reg, data, nr_regs);
	if (ret != 0)
		return ret;

	return nr_regs;
}
EXPORT_SYMBOL_GPL(pcf50633_read_block);

/* Write a block of up to 32 regs  */
int pcf50633_write_block(struct pcf50633 *pcf , u8 reg,
					int nr_regs, u8 *data)
{
	return regmap_raw_write(pcf->regmap, reg, data, nr_regs);
}
EXPORT_SYMBOL_GPL(pcf50633_write_block);

u8 pcf50633_reg_read(struct pcf50633 *pcf, u8 reg)
{
	unsigned int val;
	int ret;

	ret = regmap_read(pcf->regmap, reg, &val);
	if (ret < 0)
		return -1;

	return val;
}
EXPORT_SYMBOL_GPL(pcf50633_reg_read);

int pcf50633_reg_write(struct pcf50633 *pcf, u8 reg, u8 val)
{
	return regmap_write(pcf->regmap, reg, val);
}
EXPORT_SYMBOL_GPL(pcf50633_reg_write);

int pcf50633_reg_set_bit_mask(struct pcf50633 *pcf, u8 reg, u8 mask, u8 val)
{
	return regmap_update_bits(pcf->regmap, reg, mask, val);
}
EXPORT_SYMBOL_GPL(pcf50633_reg_set_bit_mask);

int pcf50633_reg_clear_bits(struct pcf50633 *pcf, u8 reg, u8 val)
{
	return regmap_update_bits(pcf->regmap, reg, val, 0);
}
EXPORT_SYMBOL_GPL(pcf50633_reg_clear_bits);

/* sysfs attributes */
static ssize_t show_dump_regs(struct device *dev, struct device_attribute *attr,
			    char *buf)
{
	struct pcf50633 *pcf = dev_get_drvdata(dev);
	u8 dump[16];
	int n, n1, idx = 0;
	char *buf1 = buf;
	static u8 address_no_read[] = { /* must be ascending */
		PCF50633_REG_INT1,
		PCF50633_REG_INT2,
		PCF50633_REG_INT3,
		PCF50633_REG_INT4,
		PCF50633_REG_INT5,
		0 /* terminator */
	};

	for (n = 0; n < 256; n += sizeof(dump)) {
		for (n1 = 0; n1 < sizeof(dump); n1++)
			if (n == address_no_read[idx]) {
				idx++;
				dump[n1] = 0x00;
			} else
				dump[n1] = pcf50633_reg_read(pcf, n + n1);

		hex_dump_to_buffer(dump, sizeof(dump), 16, 1, buf1, 128, 0);
		buf1 += strlen(buf1);
		*buf1++ = '\n';
		*buf1 = '\0';
	}

	return buf1 - buf;
}
static DEVICE_ATTR(dump_regs, 0400, show_dump_regs, NULL);

static ssize_t show_resume_reason(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct pcf50633 *pcf = dev_get_drvdata(dev);
	int n;

	n = sprintf(buf, "%02x%02x%02x%02x%02x\n",
				pcf->resume_reason[0],
				pcf->resume_reason[1],
				pcf->resume_reason[2],
				pcf->resume_reason[3],
				pcf->resume_reason[4]);

	return n;
}
static DEVICE_ATTR(resume_reason, 0400, show_resume_reason, NULL);

static struct attribute *pcf_sysfs_entries[] = {
	&dev_attr_dump_regs.attr,
	&dev_attr_resume_reason.attr,
	NULL,
};

static struct attribute_group pcf_attr_group = {
	.name	= NULL,			/* put in device directory */
	.attrs	= pcf_sysfs_entries,
};

static void
pcf50633_client_dev_register(struct pcf50633 *pcf, const char *name,
						struct platform_device **pdev)
{
	int ret;

	*pdev = platform_device_alloc(name, -1);
	if (!*pdev) {
		dev_err(pcf->dev, "Falied to allocate %s\n", name);
		return;
	}

	(*pdev)->dev.parent = pcf->dev;

	ret = platform_device_add(*pdev);
	if (ret) {
		dev_err(pcf->dev, "Failed to register %s: %d\n", name, ret);
		platform_device_put(*pdev);
		*pdev = NULL;
	}
}

#ifdef CONFIG_PM_SLEEP
static int pcf50633_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct pcf50633 *pcf = i2c_get_clientdata(client);

	return pcf50633_irq_suspend(pcf);
}

static int pcf50633_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct pcf50633 *pcf = i2c_get_clientdata(client);

	return pcf50633_irq_resume(pcf);
}
#endif

static SIMPLE_DEV_PM_OPS(pcf50633_pm, pcf50633_suspend, pcf50633_resume);

static struct regmap_config pcf50633_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
};

static int __devinit pcf50633_probe(struct i2c_client *client,
				const struct i2c_device_id *ids)
{
	struct pcf50633 *pcf;
	struct pcf50633_platform_data *pdata = client->dev.platform_data;
	int i, ret;
	int version, variant;

	if (!client->irq) {
		dev_err(&client->dev, "Missing IRQ\n");
		return -ENOENT;
	}

	pcf = devm_kzalloc(&client->dev, sizeof(*pcf), GFP_KERNEL);
	if (!pcf)
		return -ENOMEM;

	pcf->pdata = pdata;

	mutex_init(&pcf->lock);

	pcf->regmap = devm_regmap_init_i2c(client, &pcf50633_regmap_config);
	if (IS_ERR(pcf->regmap)) {
		ret = PTR_ERR(pcf->regmap);
		dev_err(pcf->dev, "Failed to allocate register map: %d\n", ret);
		return ret;
	}

	i2c_set_clientdata(client, pcf);
	pcf->dev = &client->dev;

	version = pcf50633_reg_read(pcf, 0);
	variant = pcf50633_reg_read(pcf, 1);
	if (version < 0 || variant < 0) {
		dev_err(pcf->dev, "Unable to probe pcf50633\n");
		ret = -ENODEV;
		return ret;
	}

	dev_info(pcf->dev, "Probed device version %d variant %d\n",
							version, variant);

	pcf50633_irq_init(pcf, client->irq);

	/* Create sub devices */
	pcf50633_client_dev_register(pcf, "pcf50633-input", &pcf->input_pdev);
	pcf50633_client_dev_register(pcf, "pcf50633-rtc", &pcf->rtc_pdev);
	pcf50633_client_dev_register(pcf, "pcf50633-mbc", &pcf->mbc_pdev);
	pcf50633_client_dev_register(pcf, "pcf50633-adc", &pcf->adc_pdev);
	pcf50633_client_dev_register(pcf, "pcf50633-backlight", &pcf->bl_pdev);


	for (i = 0; i < PCF50633_NUM_REGULATORS; i++) {
		struct platform_device *pdev;

		pdev = platform_device_alloc("pcf50633-regltr", i);
		if (!pdev) {
			dev_err(pcf->dev, "Cannot create regulator %d\n", i);
			continue;
		}

		pdev->dev.parent = pcf->dev;
		if (platform_device_add_data(pdev, &pdata->reg_init_data[i],
					sizeof(pdata->reg_init_data[i])) < 0) {
			platform_device_put(pdev);
			dev_err(pcf->dev, "Out of memory for regulator parameters %d\n",
									i);
			continue;
		}
		pcf->regulator_pdev[i] = pdev;

		platform_device_add(pdev);
	}

	ret = sysfs_create_group(&client->dev.kobj, &pcf_attr_group);
	if (ret)
		dev_err(pcf->dev, "error creating sysfs entries\n");

	if (pdata->probe_done)
		pdata->probe_done(pcf);

	return 0;
}

static int __devexit pcf50633_remove(struct i2c_client *client)
{
	struct pcf50633 *pcf = i2c_get_clientdata(client);
	int i;

	sysfs_remove_group(&client->dev.kobj, &pcf_attr_group);
	pcf50633_irq_free(pcf);

	platform_device_unregister(pcf->input_pdev);
	platform_device_unregister(pcf->rtc_pdev);
	platform_device_unregister(pcf->mbc_pdev);
	platform_device_unregister(pcf->adc_pdev);
	platform_device_unregister(pcf->bl_pdev);

	for (i = 0; i < PCF50633_NUM_REGULATORS; i++)
		platform_device_unregister(pcf->regulator_pdev[i]);

	return 0;
}

static const struct i2c_device_id pcf50633_id_table[] = {
	{"pcf50633", 0x73},
	{/* end of list */}
};
MODULE_DEVICE_TABLE(i2c, pcf50633_id_table);

static struct i2c_driver pcf50633_driver = {
	.driver = {
		.name	= "pcf50633",
		.pm	= &pcf50633_pm,
	},
	.id_table = pcf50633_id_table,
	.probe = pcf50633_probe,
	.remove = __devexit_p(pcf50633_remove),
};

static int __init pcf50633_init(void)
{
	return i2c_add_driver(&pcf50633_driver);
}

static void __exit pcf50633_exit(void)
{
	i2c_del_driver(&pcf50633_driver);
}

MODULE_DESCRIPTION("I2C chip driver for NXP PCF50633 PMU");
MODULE_AUTHOR("Harald Welte <laforge@openmoko.org>");
MODULE_LICENSE("GPL");

subsys_initcall(pcf50633_init);
module_exit(pcf50633_exit);
