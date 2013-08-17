/* linux/arch/arm/mach-exynos/setup-fimc-sensor.c
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * FIMC-IS gpio and clock configuration
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/gpio.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/regulator/consumer.h>
#include <linux/delay.h>
#include <mach/regs-gpio.h>
#include <mach/map.h>
#include <mach/regs-clock.h>
#include <plat/clock.h>
#include <plat/gpio-cfg.h>
#include <plat/map-s5p.h>
#include <plat/cpu.h>
#include <media/exynos_fimc_is_sensor.h>
#include <mach/exynos-clock.h>

struct device; /* don't need the contents */

int exynos5_fimc_is_sensor_gpio(struct device *pdev, struct gpio_set *gpio, int flag_on)
{
	int ret = 0;

	pr_debug("exynos5_fimc_is_sensor_gpio\n");

	ret = gpio_request(gpio->pin, gpio->name);
	if (ret) {
		pr_err("Request GPIO error(%s)\n", gpio->name);
		return ret;
	}

	if (flag_on == 1) {
		switch (gpio->act) {
		case GPIO_PULL_NONE:
			s3c_gpio_cfgpin(gpio->pin, gpio->value);
			s3c_gpio_setpull(gpio->pin, S3C_GPIO_PULL_NONE);
			/* set max strength */
			if (strstr(gpio->name, "SDA") || strstr(gpio->name, "SCL"))
				s5p_gpio_set_drvstr(gpio->pin, S5P_GPIO_DRVSTR_LV4);
			break;
		case GPIO_OUTPUT:
			s3c_gpio_cfgpin(gpio->pin, S3C_GPIO_OUTPUT);
			s3c_gpio_setpull(gpio->pin, S3C_GPIO_PULL_NONE);
			if (flag_on == 1)
				gpio_set_value(gpio->pin, gpio->value);
			else
				gpio_set_value(gpio->pin, !gpio->value);
			break;
		case GPIO_INPUT:
			s3c_gpio_cfgpin(gpio->pin, S3C_GPIO_INPUT);
			s3c_gpio_setpull(gpio->pin, S3C_GPIO_PULL_NONE);
			gpio_set_value(gpio->pin, gpio->value);
			break;
		case GPIO_RESET:
			s3c_gpio_setpull(gpio->pin, S3C_GPIO_PULL_NONE);
			gpio_direction_output(gpio->pin, 0);
			gpio_direction_output(gpio->pin, 1);
			break;
		default:
			pr_err("unknown act for gpio\n");
			break;
		}
	} else {
		s3c_gpio_cfgpin(gpio->pin, S3C_GPIO_INPUT);
		s3c_gpio_setpull(gpio->pin, S3C_GPIO_PULL_DOWN);
	}

	gpio_free(gpio->pin);

	return ret;
}

int exynos5_fimc_is_sensor_regulator(struct device *pdev, struct gpio_set *gpio, int flag_on)
{
	int ret = 0;

	if (soc_is_exynos5410()) {
		struct regulator *regulator = NULL;

		if (flag_on == 1) {
			regulator = regulator_get(pdev, gpio->name);
			if (IS_ERR(regulator)) {
				pr_err("%s : regulator_get(%s) fail\n",
					__func__, gpio->name);
				return PTR_ERR(regulator);
			} else if (!regulator_is_enabled(regulator)) {
				ret = regulator_enable(regulator);
				if (ret) {
					pr_err("%s : regulator_enable(%s) fail\n",
						__func__, gpio->name);
					regulator_put(regulator);
					return ret;
				}
			}
			regulator_put(regulator);
		} else {
			regulator = regulator_get(pdev, gpio->name);
			if (IS_ERR(regulator)) {
				pr_err("%s : regulator_get(%s) fail\n",
					__func__, gpio->name);
				return PTR_ERR(regulator);
			} else if (regulator_is_enabled(regulator)) {
				ret = regulator_disable(regulator);
				if (ret) {
					pr_err("%s : regulator_disable(%s) fail\n",
						__func__, gpio->name);
					regulator_put(regulator);
					return ret;
				}
			}
			regulator_put(regulator);
		}
	}

	return ret;
}

int exynos5_fimc_is_sensor_pin_cfg(struct device *pdev, int sensor_id, int flag_on)
{
	int ret = 0;
	int i = 0;

	struct exynos_fimc_is_sensor_platform_data *dev = pdev->platform_data;
	struct gpio_set *gpio;

	pr_debug("exynos5_fimc_is_sensor_pin_cfg\n");

	if (flag_on == 1) {
		for (i = 0; i < FIMC_IS_MAX_GPIO_NUM; i++) {
			gpio = &dev->gpio_info->cfg[i];

			if (!gpio->pin_type)
				continue;

			/*
			 * TODO: check the camera ID
			 *if (gpio->flite_id != FLITE_ID_END &&
			 *	gpio->flite_id != sensor_id)
			 *	continue;
			 */

			if (gpio->count == 0) {
				/* insert timing */
				/* iT0 */
				if (strcmp("cam_isp_sensor_io_1.8v", gpio->name) == 0)
					usleep_range(2000, 2000);
				/* iT1 */
				if (strcmp("GPIO_MAIN_CAM_RESET", gpio->name) == 0)
					usleep_range(2000, 2000);

				if (strcmp("GPIO_CAM_VT_nRST", gpio->name) == 0)
					continue;

				/* stT0 */
				if (strcmp("GPIO_CAM_VT_STBY", gpio->name) == 0)
					usleep_range(1000, 1000);

				switch(gpio->pin_type) {
				case PIN_GPIO:
					ret = exynos5_fimc_is_sensor_gpio(pdev, gpio, flag_on);
					if (ret)
						pr_err("%s : exynos5_fimc_is_sensor_gpio failed\n", __func__);
					break;
				case PIN_REGULATOR:
					ret = exynos5_fimc_is_sensor_regulator(pdev, gpio, flag_on);
					if (ret)
						pr_err("%s : exynos5_fimc_is_sensor_regulator failed\n", __func__);
					break;
				default:
					break;
				}

				/* iT2 + iT3*/
				if (strcmp("GPIO_MAIN_CAM_RESET", gpio->name) == 0)
					usleep_range(1000, 1000);

				/* stT1 + stT2 */
				if (strcmp("GPIO_VT_CAM_MCLK", gpio->name) == 0)
					usleep_range(1000, 1000);
			}

			if (ret == 0)
				gpio->count++;
		}
	} else {
		for (i = FIMC_IS_MAX_GPIO_NUM - 1; i >= 0; i--) {
			gpio = &dev->gpio_info->cfg[i];

			if (!gpio->pin_type)
				continue;

			/*
			 * TODO: check the camera ID
			 *if (gpio->flite_id != FLITE_ID_END &&
			 *	gpio->flite_id != sensor_id)
			 *	continue;
			 */

			if (gpio->count == 1) {
				switch(gpio->pin_type) {
				case PIN_GPIO:
					ret = exynos5_fimc_is_sensor_gpio(pdev, gpio, flag_on);
					if (ret)
						pr_err("%s : exynos5_fimc_is_sensor_gpio failed\n", __func__);
					break;
				case PIN_REGULATOR:
					ret = exynos5_fimc_is_sensor_regulator(pdev, gpio, flag_on);
					if (ret)
						pr_err("%s : exynos5_fimc_is_sensor_regulator failed\n", __func__);
					break;
				default:
					break;
				}
			}

			if (ret == 0 && 0 < gpio->count)
				gpio->count--;
		}
	}

	return ret;

}

int exynos5_fimc_is_sensor_clk_on(struct device *pdev, int sensor_id)
{
	int ret = 0;

	pr_debug("exynos5_fimc_is_sensor_clk_on\n");

	ret = exynos5_fimc_is_sensor_pin_cfg(pdev, sensor_id, 1);
	if (ret) {
		pr_err("%s : exynos5_fimc_is_sensor_pin_cfg(%d, 1) failed\n", __func__, sensor_id);
		return ret;
	}

	return 0;
}

int exynos5_fimc_is_sensor_clk_off(struct device *pdev, int sensor_id)
{
	int ret = 0;

	pr_debug("exynos5_fimc_is_clk_off\n");

	ret = exynos5_fimc_is_sensor_pin_cfg(pdev, sensor_id, 0);
	if (ret) {
		pr_err("%s : exynos5_fimc_is_sensor_pin_cfg(%d, 0) failed\n", __func__, sensor_id);
		return ret;
	}

	return 0;
}
