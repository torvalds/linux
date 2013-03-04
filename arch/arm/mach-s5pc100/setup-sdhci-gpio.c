/* linux/arch/arm/plat-s5pc100/setup-sdhci-gpio.c
 *
 * Copyright 2009 Samsung Eletronics
 *
 * S5PC100 - Helper functions for setting up SDHCI device(s) GPIO (HSMMC)
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
#include <plat/sdhci.h>

void s5pc100_setup_sdhci0_cfg_gpio(struct platform_device *dev, int width)
{
	struct s3c_sdhci_platdata *pdata = dev->dev.platform_data;
	unsigned int num;

	num = width;
	/* In case of 8 width, we should decrease the 2 */
	if (width == 8)
		num = width - 2;

	/* Set all the necessary GPG0/GPG1 pins to special-function 0 */
	s3c_gpio_cfgrange_nopull(S5PC100_GPG0(0), 2 + num, S3C_GPIO_SFN(2));

	if (width == 8)
		s3c_gpio_cfgrange_nopull(S5PC100_GPG1(0), 2, S3C_GPIO_SFN(2));

	if (pdata->cd_type == S3C_SDHCI_CD_INTERNAL) {
		s3c_gpio_setpull(S5PC100_GPG1(2), S3C_GPIO_PULL_UP);
		s3c_gpio_cfgpin(S5PC100_GPG1(2), S3C_GPIO_SFN(2));
	}
}

void s5pc100_setup_sdhci1_cfg_gpio(struct platform_device *dev, int width)
{
	struct s3c_sdhci_platdata *pdata = dev->dev.platform_data;

	/* Set all the necessary GPG2 pins to special-function 2 */
	s3c_gpio_cfgrange_nopull(S5PC100_GPG2(0), 2 + width, S3C_GPIO_SFN(2));

	if (pdata->cd_type == S3C_SDHCI_CD_INTERNAL) {
		s3c_gpio_setpull(S5PC100_GPG2(6), S3C_GPIO_PULL_UP);
		s3c_gpio_cfgpin(S5PC100_GPG2(6), S3C_GPIO_SFN(2));
	}
}

void s5pc100_setup_sdhci2_cfg_gpio(struct platform_device *dev, int width)
{
	struct s3c_sdhci_platdata *pdata = dev->dev.platform_data;

	/* Set all the necessary GPG3 pins to special-function 2 */
	s3c_gpio_cfgrange_nopull(S5PC100_GPG3(0), 2 + width, S3C_GPIO_SFN(2));

	if (pdata->cd_type == S3C_SDHCI_CD_INTERNAL) {
		s3c_gpio_setpull(S5PC100_GPG3(6), S3C_GPIO_PULL_UP);
		s3c_gpio_cfgpin(S5PC100_GPG3(6), S3C_GPIO_SFN(2));
	}
}
