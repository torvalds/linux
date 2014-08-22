/*
 *  drivers/mfd/rt5025-core.c
 *  Driver for Richtek RT5025 Core PMIC
 *
 *  Copyright (C) 2014 Richtek Technology Corp.
 *  cy_huang <cy_huang@richtek.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/version.h>
#include <linux/i2c.h>
#include <linux/mfd/core.h>
#include <linux/mfd/rt5025.h>

#ifdef CONFIG_REGULATOR_RT5025
#ifdef CONFIG_OF
#define RT5025_BUCKVR_DEVS(_id, _idx)					\
{									\
	.name		= RT5025_DEV_NAME "-regulator",		\
	.num_resources	= 0,						\
	.of_compatible	= "rt," RT5025_DEV_NAME "-dcdc" #_idx , 	\
	.id		= RT5025_ID_##_id,				\
}

#define RT5025_LDOVR_DEVS(_id, _idx)					\
{									\
	.name		= RT5025_DEV_NAME "-regulator",		\
	.num_resources	= 0,						\
	.of_compatible	= "rt," RT5025_DEV_NAME "-ldo" #_idx , 	\
	.id		= RT5025_ID_##_id,				\
}
#else
#define RT5025_BUCKVR_DEVS(_id, _idx)					\
{									\
	.name		= RT5025_DEV_NAME "-regulator",		\
	.num_resources	= 0,						\
	.id		= RT5025_ID_##_id,				\
}

#define RT5025_LDOVR_DEVS(_id, _idx)					\
{									\
	.name		= RT5025_DEV_NAME "-regulator",		\
	.num_resources	= 0,						\
	.id		= RT5025_ID_##_id,				\
}
#endif /* #ifdef CONFIG_OF */
static struct mfd_cell regulator_devs[] = {
	RT5025_BUCKVR_DEVS(DCDC1, 1),
	RT5025_BUCKVR_DEVS(DCDC2, 2),
	RT5025_BUCKVR_DEVS(DCDC3, 3),
	RT5025_BUCKVR_DEVS(DCDC4, 4),
	RT5025_LDOVR_DEVS(LDO1,   1),
	RT5025_LDOVR_DEVS(LDO2,   2),
	RT5025_LDOVR_DEVS(LDO3,   3),
	RT5025_LDOVR_DEVS(LDO4,   4),
	RT5025_LDOVR_DEVS(LDO5,   5),
	RT5025_LDOVR_DEVS(LDO6,   6),
};
#endif /* #ifdef CONFIG_REGULATOR_RT5025 */

#ifdef CONFIG_CHARGER_RT5025
static struct mfd_cell chg_devs[] = {
{
	.name = RT5025_DEV_NAME "-charger",
	.id = -1,
	.num_resources = 0,
#ifdef CONFIG_OF
	.of_compatible	= "rt," RT5025_DEV_NAME "-charger" ,
#endif /*#ifdef CONFIG_OF */
},
};
#endif /* #ifdef CONFIG_CHARGER_RT5025 */

#ifdef CONFIG_BATTERY_RT5025
static struct mfd_cell fg_devs[] = {
{
	.name = RT5025_DEV_NAME "-battery",
	.id = -1,
	.num_resources = 0,
#ifdef CONFIG_OF
	.of_compatible	= "rt," RT5025_DEV_NAME "-battery" ,
#endif /*#ifdef CONFIG_OF */
},
};
#endif /* #ifdef CONFIG_BATTERY_RT5025 */

#ifdef CONFIG_GPIO_RT5025
static struct mfd_cell gpio_devs[] = {
{
	.name = RT5025_DEV_NAME "-gpio",
	.id = -1,
	.num_resources = 0,
#ifdef CONFIG_OF
	.of_compatible	= "rt," RT5025_DEV_NAME "-gpio" ,
#endif /*#ifdef CONFIG_OF */
},
};
#endif /* #ifdef CONFIG_GPIO_RT5025 */

#ifdef CONFIG_MISC_RT5025
static struct mfd_cell misc_devs[] = {
{
	.name = RT5025_DEV_NAME "-misc",
	.id = -1,
	.num_resources = 0,
#ifdef CONFIG_OF
	.of_compatible	= "rt," RT5025_DEV_NAME "-misc" ,
#endif /*#ifdef CONFIG_OF */
},
};
#endif /* #ifdef CONFIG_MISC_RT5025 */

#ifdef CONFIG_IRQ_RT5025
static struct mfd_cell irq_devs[] = {
{
	.name = RT5025_DEV_NAME "-irq",
	.id = -1,
	.num_resources = 0,
#ifdef CONFIG_OF
	.of_compatible	= "rt," RT5025_DEV_NAME "-irq" ,
#endif /*#ifdef CONFIG_OF */
},
};
#endif /* #ifdef CONFIG_IRQ_RT5025 */

#ifdef CONFIG_DEBUG_RT5025
static struct mfd_cell debug_devs[] = {
{
	.name = RT5025_DEV_NAME "-debug",
	.id = -1,
	.num_resources = 0,
#ifdef CONFIG_OF
	.of_compatible	= "rt," RT5025_DEV_NAME "-debug" ,
#endif /*#ifdef CONFIG_OF */
},
};
#endif /* #ifdef CONFIG_DEBUG_RT5025 */

int rt5025_core_init(struct rt5025_chip *chip,
	struct rt5025_platform_data *pdata)
{
	int ret = 0;
	bool use_dt = chip->dev->of_node;

	RTINFO("Start to initialize all device\n");

	#ifdef CONFIG_REGULATOR_RT5025
	if (use_dt || (pdata && pdata->regulator[0])) {
		RTINFO("mfd add regulators dev\n");
		#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 6, 0))
		ret = mfd_add_devices(chip->dev, 0, &regulator_devs[0],
				      ARRAY_SIZE(regulator_devs),
				      NULL, 0, NULL);
		#else
		ret = mfd_add_devices(chip->dev, 0, &regulator_devs[0],
				      ARRAY_SIZE(regulator_devs),
				      NULL, 0);
		#endif /* LINUX_VERSION_CODE >= KERNL_VERSION(3,6,0) */
		if (ret < 0) {
			dev_err(chip->dev, "Failed to add regulator subdev\n");
			goto out_dev;
		}
	}
	#endif /* #ifdef CONFIG_REGULATOR_RT5025 */

	#ifdef CONFIG_CHARGER_RT5025
	if (use_dt || (pdata && pdata->chg_pdata)) {
	    RTINFO("mfd add charger dev\n");
		#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 6, 0))
		ret = mfd_add_devices(chip->dev, 0, &chg_devs[0],
					ARRAY_SIZE(chg_devs),
					NULL, 0, NULL);
		#else
		ret = mfd_add_devices(chip->dev, 0, &chg_devs[0],
					ARRAY_SIZE(chg_devs),
					NULL, 0);
		#endif
		if (ret < 0) {
			dev_err(chip->dev, "Failed to add power supply subdev\n");
			goto out_dev;
		}
	}
	#endif /* #ifdef CONFIG_CHARGER_RT5025 */

	#ifdef CONFIG_BATTERY_RT5025
	if (use_dt || (pdata)) {
	    RTINFO("mfd add fuelgauge dev\n");
		#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 6, 0))
		ret = mfd_add_devices(chip->dev, 0, &fg_devs[0],
					ARRAY_SIZE(fg_devs),
					NULL, 0, NULL);
		#else
		ret = mfd_add_devices(chip->dev, 0, &fg_devs[0],
					ARRAY_SIZE(fg_devs),
					NULL, 0);
		#endif
		if (ret < 0) {
			dev_err(chip->dev, "Failed to add power supply "
				"subdev\n");
			goto out_dev;
		}
	}
	#endif /* #ifdef CONFIG_BATTERY_RT5025 */

	/*Initialize the RT5025_GPIO*/
	#ifdef CONFIG_GPIO_RT5025
	if (use_dt || (pdata && pdata->gpio_pdata)) {
		RTINFO("mfd add gpios dev\n");
		#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 6, 0))
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
	#endif /* #ifdef CONFIG_GPIO_RT5025 */

	#ifdef CONFIG_MISC_RT5025
	if (use_dt || (pdata && pdata->misc_pdata)) {
		RTINFO("mfd add misc dev\n");
		#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 6, 0))
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
	#endif /* #ifdef CONFIG_MISC_RT5025 */

	#ifdef CONFIG_IRQ_RT5025
	if (use_dt || (pdata && pdata->irq_pdata)) {
		RTINFO("mfd add irq dev\n");
		#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 6, 0))
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
	#endif /* #ifdef CONFIG_IRQ_RT5025 */

	#ifdef CONFIG_DEBUG_RT5025
	RTINFO("mfd add debug dev\n");
	#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 6, 0))
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
	#endif /* CONFIG_DEBUG_RT5025 */

	RTINFO("Initialize all device successfully\n");
	return ret;
out_dev:
	mfd_remove_devices(chip->dev);
	return ret;
}
EXPORT_SYMBOL(rt5025_core_init);

int rt5025_core_deinit(struct rt5025_chip *chip)
{
	mfd_remove_devices(chip->dev);
	return 0;
}
EXPORT_SYMBOL(rt5025_core_deinit);
