/*
 *  drivers/mfd/rt5025-core.c
 *  Driver for Richtek RT5025 Core PMIC
 *
 *  Copyright (C) 2013 Richtek Electronics
 *  cy_huang <cy_huang@richtek.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/version.h>
#include <linux/i2c.h>
#include <linux/mfd/core.h>

#include <linux/mfd/rt5025.h>

#ifdef CONFIG_REGULATOR_RT5025
#define RT5025_VR_DEVS(_id)						\
{									\
	.name		= RT5025_DEVICE_NAME "-regulator",				\
	.num_resources	= 0,						\
	.id		= RT5025_ID_##_id,				\
}

static struct mfd_cell regulator_devs[] = {
	RT5025_VR_DEVS(DCDC1),
	RT5025_VR_DEVS(DCDC2),
	RT5025_VR_DEVS(DCDC3),
	RT5025_VR_DEVS(DCDC4),
	RT5025_VR_DEVS(LDO1),
	RT5025_VR_DEVS(LDO2),
	RT5025_VR_DEVS(LDO3),
	RT5025_VR_DEVS(LDO4),
	RT5025_VR_DEVS(LDO5),
	RT5025_VR_DEVS(LDO6),
};
#endif /* CONFIG_REGULATOR_RT5025 */

#ifdef CONFIG_POWER_RT5025
static struct mfd_cell power_devs[] = {
{
	.name = RT5025_DEVICE_NAME "-power",
	.id = -1,
	.num_resources = 0,
},
{
	.name = RT5025_DEVICE_NAME "-swjeita",
	.id = -1,
	.num_resources = 0,
},
{
	.name = RT5025_DEVICE_NAME "-battery",
	.id = -1,
	.num_resources = 0,
},
};
#endif /* CONFIG_POWER_RT5025 */

#ifdef CONFIG_GPIO_RT5025
static struct mfd_cell gpio_devs[] = {
{
	.name = RT5025_DEVICE_NAME "-gpio",
	.id = -1,
	.num_resources = 0,
},
};
#endif /* CONFIG_GPIO_RT5025 */

#ifdef CONFIG_MFD_RT5025_MISC
static struct mfd_cell misc_devs[] = {
{
	.name = RT5025_DEVICE_NAME "-misc",
	.id = -1,
	.num_resources = 0,
},
};
#endif /* CONFIG_MFD_RT5025_MISC */

#ifdef CONFIG_MFD_RT5025_IRQ
static struct mfd_cell irq_devs[] = {
{
	.name = RT5025_DEVICE_NAME "-irq",
	.id = -1,
	.num_resources = 0,
},
};
#endif /* CONFIG_MFD_RT5025_IRQ */

#ifdef CONFIG_MFD_RT5025_DEBUG
static struct mfd_cell debug_devs[] = {
{
	.name = RT5025_DEVICE_NAME "-debug",
	.id = -1,
	.num_resources = 0,
},
};
#endif /* CONFIG_MFD_RT5025_DEBUG */

int __devinit rt5025_core_init(struct rt5025_chip *chip, struct rt5025_platform_data *pdata)
{
	int ret = 0;

	RTINFO("Start to initialize all device\n");

	#ifdef CONFIG_REGULATOR_RT5025
	if (pdata && pdata->regulator[0]) {
		RTINFO("mfd add regulators dev\n");
		#if (LINUX_VERSION_CODE>=KERNEL_VERSION(3,6,0))
		ret = mfd_add_devices(chip->dev, 0, &regulator_devs[0],
				      ARRAY_SIZE(regulator_devs),
				      NULL, 0, NULL);
		#else
		ret = mfd_add_devices(chip->dev, 0, &regulator_devs[0],
				      ARRAY_SIZE(regulator_devs),
				      NULL, 0);
		#endif /* LINUX_VERSION_CODE>=KERNL_VERSION(3,6,0) */
		if (ret < 0) {
			dev_err(chip->dev, "Failed to add regulator subdev\n");
			goto out_dev;
		}
	}
	#endif /* CONFIG_REGULATOR_RT5025 */

	#ifdef CONFIG_POWER_RT5025
	if (pdata && pdata->power_data && pdata->jeita_data) {
	    RTINFO("mfd add power dev\n");
		#if (LINUX_VERSION_CODE>=KERNEL_VERSION(3,6,0))
		ret = mfd_add_devices(chip->dev, 0, &power_devs[0],
					ARRAY_SIZE(power_devs),
					NULL, 0,NULL);
		#else
		ret = mfd_add_devices(chip->dev, 0, &power_devs[0],
					ARRAY_SIZE(power_devs),
					NULL, 0);
		#endif
		if (ret < 0) {
			dev_err(chip->dev, "Failed to add power supply "
				"subdev\n");
			goto out_dev;
		}
	}
	#endif /* CONFIG_MFD_RT5025 */

	//Initialize the RT5025_GPIO
	#ifdef CONFIG_GPIO_RT5025
	if (pdata && pdata->gpio_data) {
		RTINFO("mfd add gpios dev\n");
		#if (LINUX_VERSION_CODE>=KERNEL_VERSION(3,6,0))
		ret = mfd_add_devices(chip->dev, 0, &gpio_devs[0],
				      ARRAY_SIZE(gpio_devs),
				      NULL, 0, NULL);
		#else
		ret = mfd_add_devices(chip->dev, 0, &gpio_devs[0],
				      ARRAY_SIZE(gpio_devs),
				      NULL, 0);
		#endif /* LINUX_VERSION_CODE>=KERNL_VERSION(3,6,0) */
		if (ret < 0) {
			dev_err(chip->dev, "Failed to add gpio subdev\n");
			goto out_dev;
		}
	}
	#endif /* CONFIG_GPIO_RT5025 */

	#ifdef CONFIG_MFD_RT5025_MISC
	if (pdata && pdata->misc_data) {
		RTINFO("mfd add misc dev\n");
		#if (LINUX_VERSION_CODE>=KERNEL_VERSION(3,6,0))
		ret = mfd_add_devices(chip->dev, 0, &misc_devs[0],
				      ARRAY_SIZE(misc_devs),
				      NULL, 0, NULL);
		#else
		ret = mfd_add_devices(chip->dev, 0, &misc_devs[0],
				      ARRAY_SIZE(misc_devs),
				      NULL, 0);
		#endif /* LINUX_VERSION_CODE>=KERNL_VERSION(3,6,0) */
		if (ret < 0) {
			dev_err(chip->dev, "Failed to add misc subdev\n");
			goto out_dev;
		}
	}
	#endif /* CONFIG_MFD_RT5025_MISC */

	#ifdef CONFIG_MFD_RT5025_IRQ
	if (pdata && pdata->irq_data) {
		RTINFO("mfd add irq dev\n");
		#if (LINUX_VERSION_CODE>=KERNEL_VERSION(3,6,0))
		ret = mfd_add_devices(chip->dev, 0, &irq_devs[0],
				      ARRAY_SIZE(irq_devs),
				      NULL, 0, NULL);
		#else
		ret = mfd_add_devices(chip->dev, 0, &irq_devs[0],
				      ARRAY_SIZE(irq_devs),
				      NULL, 0);
		#endif /* LINUX_VERSION_CODE>=KERNL_VERSION(3,6,0) */
		if (ret < 0) {
			dev_err(chip->dev, "Failed to add irq subdev\n");
			goto out_dev;
		}
	}
	#endif /* CONFIG_MFD_RT5025_IRQ */

	#ifdef CONFIG_MFD_RT5025_DEBUG
	RTINFO("mfd add debug dev\n");
	#if (LINUX_VERSION_CODE>=KERNEL_VERSION(3,6,0))
	ret = mfd_add_devices(chip->dev, 0, &debug_devs[0],
			      ARRAY_SIZE(debug_devs),
			      NULL, 0, NULL);
	#else
	ret = mfd_add_devices(chip->dev, 0, &debug_devs[0],
			      ARRAY_SIZE(debug_devs),
			      NULL, 0);
	#endif /* LINUX_VERSION_CODE>=KERNL_VERSION(3,6,0) */
	if (ret < 0) {
		dev_err(chip->dev, "Failed to add debug subdev\n");
		goto out_dev;
	}
	#endif /* CONFIG_MFD_RT5025_DEBUG */

	RTINFO("Initialize all device successfully\n");
	return ret;
out_dev:
	mfd_remove_devices(chip->dev);
	return ret;
}
EXPORT_SYMBOL(rt5025_core_init);

int __devexit rt5025_core_deinit(struct rt5025_chip *chip)
{
	mfd_remove_devices(chip->dev);
	return 0;
}
EXPORT_SYMBOL(rt5025_core_deinit);
