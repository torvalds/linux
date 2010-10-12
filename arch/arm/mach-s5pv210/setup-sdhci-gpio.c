/* linux/arch/arm/plat-s5pc1xx/setup-sdhci-gpio.c
 *
 * Copyright (c) 2009-2010 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * S5PV210 - Helper functions for setting up SDHCI device(s) GPIO (HSMMC)
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

void s5pv210_setup_sdhci0_cfg_gpio(struct platform_device *dev, int width)
{
	struct s3c_sdhci_platdata *pdata = dev->dev.platform_data;
	unsigned int gpio;

	/* Set all the necessary GPG0/GPG1 pins to special-function 2 */
	for (gpio = S5PV210_GPG0(0); gpio < S5PV210_GPG0(2); gpio++) {
		s3c_gpio_cfgpin(gpio, S3C_GPIO_SFN(2));
		s3c_gpio_setpull(gpio, S3C_GPIO_PULL_NONE);
	}
	switch (width) {
	case 8:
		/* GPG1[3:6] special-funtion 3 */
		for (gpio = S5PV210_GPG1(3); gpio <= S5PV210_GPG1(6); gpio++) {
			s3c_gpio_cfgpin(gpio, S3C_GPIO_SFN(3));
			s3c_gpio_setpull(gpio, S3C_GPIO_PULL_NONE);
		}
	case 4:
		/* GPG0[3:6] special-funtion 2 */
		for (gpio = S5PV210_GPG0(3); gpio <= S5PV210_GPG0(6); gpio++) {
			s3c_gpio_cfgpin(gpio, S3C_GPIO_SFN(2));
			s3c_gpio_setpull(gpio, S3C_GPIO_PULL_NONE);
		}
	default:
		break;
	}

	if (pdata->cd_type == S3C_SDHCI_CD_INTERNAL) {
		s3c_gpio_setpull(S5PV210_GPG0(2), S3C_GPIO_PULL_UP);
		s3c_gpio_cfgpin(S5PV210_GPG0(2), S3C_GPIO_SFN(2));
	}
}

void s5pv210_setup_sdhci1_cfg_gpio(struct platform_device *dev, int width)
{
	struct s3c_sdhci_platdata *pdata = dev->dev.platform_data;
	unsigned int gpio;

	/* Set all the necessary GPG1[0:1] pins to special-function 2 */
	for (gpio = S5PV210_GPG1(0); gpio < S5PV210_GPG1(2); gpio++) {
		s3c_gpio_cfgpin(gpio, S3C_GPIO_SFN(2));
		s3c_gpio_setpull(gpio, S3C_GPIO_PULL_NONE);
	}

	/* Data pin GPG1[3:6] to special-function 2 */
	for (gpio = S5PV210_GPG1(3); gpio <= S5PV210_GPG1(6); gpio++) {
		s3c_gpio_cfgpin(gpio, S3C_GPIO_SFN(2));
		s3c_gpio_setpull(gpio, S3C_GPIO_PULL_NONE);
	}

	if (pdata->cd_type == S3C_SDHCI_CD_INTERNAL) {
		s3c_gpio_setpull(S5PV210_GPG1(2), S3C_GPIO_PULL_UP);
		s3c_gpio_cfgpin(S5PV210_GPG1(2), S3C_GPIO_SFN(2));
	}
}

void s5pv210_setup_sdhci2_cfg_gpio(struct platform_device *dev, int width)
{
	struct s3c_sdhci_platdata *pdata = dev->dev.platform_data;
	unsigned int gpio;

	/* Set all the necessary GPG2[0:1] pins to special-function 2 */
	for (gpio = S5PV210_GPG2(0); gpio < S5PV210_GPG2(2); gpio++) {
		s3c_gpio_cfgpin(gpio, S3C_GPIO_SFN(2));
		s3c_gpio_setpull(gpio, S3C_GPIO_PULL_NONE);
	}

	switch (width) {
	case 8:
		/* Data pin GPG3[3:6] to special-function 3 */
		for (gpio = S5PV210_GPG3(3); gpio <= S5PV210_GPG3(6); gpio++) {
			s3c_gpio_cfgpin(gpio, S3C_GPIO_SFN(3));
			s3c_gpio_setpull(gpio, S3C_GPIO_PULL_NONE);
		}
	case 4:
		/* Data pin GPG2[3:6] to special-function 2 */
		for (gpio = S5PV210_GPG2(3); gpio <= S5PV210_GPG2(6); gpio++) {
			s3c_gpio_cfgpin(gpio, S3C_GPIO_SFN(2));
			s3c_gpio_setpull(gpio, S3C_GPIO_PULL_NONE);
		}
	default:
		break;
	}

	if (pdata->cd_type == S3C_SDHCI_CD_INTERNAL) {
		s3c_gpio_setpull(S5PV210_GPG2(2), S3C_GPIO_PULL_UP);
		s3c_gpio_cfgpin(S5PV210_GPG2(2), S3C_GPIO_SFN(2));
	}
}

void s5pv210_setup_sdhci3_cfg_gpio(struct platform_device *dev, int width)
{
	struct s3c_sdhci_platdata *pdata = dev->dev.platform_data;
	unsigned int gpio;

	/* Set all the necessary GPG3[0:2] pins to special-function 2 */
	for (gpio = S5PV210_GPG3(0); gpio < S5PV210_GPG3(2); gpio++) {
		s3c_gpio_cfgpin(gpio, S3C_GPIO_SFN(2));
		s3c_gpio_setpull(gpio, S3C_GPIO_PULL_NONE);
	}

	/* Data pin GPG3[3:6] to special-function 2 */
	for (gpio = S5PV210_GPG3(3); gpio <= S5PV210_GPG3(6); gpio++) {
		s3c_gpio_cfgpin(gpio, S3C_GPIO_SFN(2));
		s3c_gpio_setpull(gpio, S3C_GPIO_PULL_NONE);
	}

	if (pdata->cd_type == S3C_SDHCI_CD_INTERNAL) {
		s3c_gpio_setpull(S5PV210_GPG3(2), S3C_GPIO_PULL_UP);
		s3c_gpio_cfgpin(S5PV210_GPG3(2), S3C_GPIO_SFN(2));
	}
}
