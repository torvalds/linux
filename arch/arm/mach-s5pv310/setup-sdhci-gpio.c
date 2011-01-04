/* linux/arch/arm/mach-s5pv310/setup-sdhci-gpio.c
 *
 * Copyright (c) 2010 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * S5PV310 - Helper functions for setting up SDHCI device(s) GPIO (HSMMC)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/gpio.h>
#include <linux/mmc/host.h>
#include <linux/mmc/card.h>

#include <plat/gpio-cfg.h>
#include <plat/regs-sdhci.h>
#include <plat/sdhci.h>

void s5pv310_setup_sdhci0_cfg_gpio(struct platform_device *dev, int width)
{
	struct s3c_sdhci_platdata *pdata = dev->dev.platform_data;
	unsigned int gpio;

	/* Set all the necessary GPK0[0:1] pins to special-function 2 */
	for (gpio = S5PV310_GPK0(0); gpio < S5PV310_GPK0(2); gpio++) {
		s3c_gpio_cfgpin(gpio, S3C_GPIO_SFN(2));
		s3c_gpio_setpull(gpio, S3C_GPIO_PULL_NONE);
		s5p_gpio_set_drvstr(gpio, S5P_GPIO_DRVSTR_LV4);
	}

	switch (width) {
	case 8:
		for (gpio = S5PV310_GPK1(3); gpio <= S5PV310_GPK1(6); gpio++) {
			/* Data pin GPK1[3:6] to special-funtion 3 */
			s3c_gpio_cfgpin(gpio, S3C_GPIO_SFN(3));
			s3c_gpio_setpull(gpio, S3C_GPIO_PULL_UP);
			s5p_gpio_set_drvstr(gpio, S5P_GPIO_DRVSTR_LV4);
		}
	case 4:
		for (gpio = S5PV310_GPK0(3); gpio <= S5PV310_GPK0(6); gpio++) {
			/* Data pin GPK0[3:6] to special-funtion 2 */
			s3c_gpio_cfgpin(gpio, S3C_GPIO_SFN(2));
			s3c_gpio_setpull(gpio, S3C_GPIO_PULL_UP);
			s5p_gpio_set_drvstr(gpio, S5P_GPIO_DRVSTR_LV4);
		}
	default:
		break;
	}

	if (pdata->cd_type == S3C_SDHCI_CD_INTERNAL) {
		s3c_gpio_cfgpin(S5PV310_GPK0(2), S3C_GPIO_SFN(2));
		s3c_gpio_setpull(S5PV310_GPK0(2), S3C_GPIO_PULL_UP);
		s5p_gpio_set_drvstr(gpio, S5P_GPIO_DRVSTR_LV4);
	}
}

void s5pv310_setup_sdhci1_cfg_gpio(struct platform_device *dev, int width)
{
	struct s3c_sdhci_platdata *pdata = dev->dev.platform_data;
	unsigned int gpio;

	/* Set all the necessary GPK1[0:1] pins to special-function 2 */
	for (gpio = S5PV310_GPK1(0); gpio < S5PV310_GPK1(2); gpio++) {
		s3c_gpio_cfgpin(gpio, S3C_GPIO_SFN(2));
		s3c_gpio_setpull(gpio, S3C_GPIO_PULL_NONE);
		s5p_gpio_set_drvstr(gpio, S5P_GPIO_DRVSTR_LV4);
	}

	for (gpio = S5PV310_GPK1(3); gpio <= S5PV310_GPK1(6); gpio++) {
		/* Data pin GPK1[3:6] to special-function 2 */
		s3c_gpio_cfgpin(gpio, S3C_GPIO_SFN(2));
		s3c_gpio_setpull(gpio, S3C_GPIO_PULL_UP);
		s5p_gpio_set_drvstr(gpio, S5P_GPIO_DRVSTR_LV4);
	}

	if (pdata->cd_type == S3C_SDHCI_CD_INTERNAL) {
		s3c_gpio_cfgpin(S5PV310_GPK1(2), S3C_GPIO_SFN(2));
		s3c_gpio_setpull(S5PV310_GPK1(2), S3C_GPIO_PULL_UP);
		s5p_gpio_set_drvstr(gpio, S5P_GPIO_DRVSTR_LV4);
	}
}

void s5pv310_setup_sdhci2_cfg_gpio(struct platform_device *dev, int width)
{
	struct s3c_sdhci_platdata *pdata = dev->dev.platform_data;
	unsigned int gpio;

	/* Set all the necessary GPK2[0:1] pins to special-function 2 */
	for (gpio = S5PV310_GPK2(0); gpio < S5PV310_GPK2(2); gpio++) {
		s3c_gpio_cfgpin(gpio, S3C_GPIO_SFN(2));
		s3c_gpio_setpull(gpio, S3C_GPIO_PULL_NONE);
		s5p_gpio_set_drvstr(gpio, S5P_GPIO_DRVSTR_LV4);
	}

	switch (width) {
	case 8:
		for (gpio = S5PV310_GPK3(3); gpio <= S5PV310_GPK3(6); gpio++) {
			/* Data pin GPK3[3:6] to special-function 3 */
			s3c_gpio_cfgpin(gpio, S3C_GPIO_SFN(3));
			s3c_gpio_setpull(gpio, S3C_GPIO_PULL_UP);
			s5p_gpio_set_drvstr(gpio, S5P_GPIO_DRVSTR_LV4);
		}
	case 4:
		for (gpio = S5PV310_GPK2(3); gpio <= S5PV310_GPK2(6); gpio++) {
			/* Data pin GPK2[3:6] to special-function 2 */
			s3c_gpio_cfgpin(gpio, S3C_GPIO_SFN(2));
			s3c_gpio_setpull(gpio, S3C_GPIO_PULL_UP);
			s5p_gpio_set_drvstr(gpio, S5P_GPIO_DRVSTR_LV4);
		}
	default:
		break;
	}

	if (pdata->cd_type == S3C_SDHCI_CD_INTERNAL) {
		s3c_gpio_cfgpin(S5PV310_GPK2(2), S3C_GPIO_SFN(2));
		s3c_gpio_setpull(S5PV310_GPK2(2), S3C_GPIO_PULL_UP);
		s5p_gpio_set_drvstr(gpio, S5P_GPIO_DRVSTR_LV4);
	}
}

void s5pv310_setup_sdhci3_cfg_gpio(struct platform_device *dev, int width)
{
	struct s3c_sdhci_platdata *pdata = dev->dev.platform_data;
	unsigned int gpio;

	/* Set all the necessary GPK3[0:1] pins to special-function 2 */
	for (gpio = S5PV310_GPK3(0); gpio < S5PV310_GPK3(2); gpio++) {
		s3c_gpio_cfgpin(gpio, S3C_GPIO_SFN(2));
		s3c_gpio_setpull(gpio, S3C_GPIO_PULL_NONE);
		s5p_gpio_set_drvstr(gpio, S5P_GPIO_DRVSTR_LV4);
	}

	for (gpio = S5PV310_GPK3(3); gpio <= S5PV310_GPK3(6); gpio++) {
		/* Data pin GPK3[3:6] to special-function 2 */
		s3c_gpio_cfgpin(gpio, S3C_GPIO_SFN(2));
		s3c_gpio_setpull(gpio, S3C_GPIO_PULL_UP);
		s5p_gpio_set_drvstr(gpio, S5P_GPIO_DRVSTR_LV4);
	}

	if (pdata->cd_type == S3C_SDHCI_CD_INTERNAL) {
		s3c_gpio_cfgpin(S5PV310_GPK3(2), S3C_GPIO_SFN(2));
		s3c_gpio_setpull(S5PV310_GPK3(2), S3C_GPIO_PULL_UP);
		s5p_gpio_set_drvstr(gpio, S5P_GPIO_DRVSTR_LV4);
	}
}
