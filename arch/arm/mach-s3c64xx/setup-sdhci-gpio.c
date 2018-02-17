// SPDX-License-Identifier: GPL-2.0
//
// Copyright 2008 Simtec Electronics
//	Ben Dooks <ben@simtec.co.uk>
//	http://armlinux.simtec.co.uk/
//
// S3C64XX - Helper functions for setting up SDHCI device(s) GPIO (HSMMC)

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/gpio.h>

#include <plat/gpio-cfg.h>
#include <plat/sdhci.h>
#include <mach/gpio-samsung.h>

void s3c64xx_setup_sdhci0_cfg_gpio(struct platform_device *dev, int width)
{
	struct s3c_sdhci_platdata *pdata = dev->dev.platform_data;

	/* Set all the necessary GPG pins to special-function 2 */
	s3c_gpio_cfgrange_nopull(S3C64XX_GPG(0), 2 + width, S3C_GPIO_SFN(2));

	if (pdata->cd_type == S3C_SDHCI_CD_INTERNAL) {
		s3c_gpio_setpull(S3C64XX_GPG(6), S3C_GPIO_PULL_UP);
		s3c_gpio_cfgpin(S3C64XX_GPG(6), S3C_GPIO_SFN(2));
	}
}

void s3c64xx_setup_sdhci1_cfg_gpio(struct platform_device *dev, int width)
{
	struct s3c_sdhci_platdata *pdata = dev->dev.platform_data;

	/* Set all the necessary GPH pins to special-function 2 */
	s3c_gpio_cfgrange_nopull(S3C64XX_GPH(0), 2 + width, S3C_GPIO_SFN(2));

	if (pdata->cd_type == S3C_SDHCI_CD_INTERNAL) {
		s3c_gpio_setpull(S3C64XX_GPG(6), S3C_GPIO_PULL_UP);
		s3c_gpio_cfgpin(S3C64XX_GPG(6), S3C_GPIO_SFN(3));
	}
}

void s3c64xx_setup_sdhci2_cfg_gpio(struct platform_device *dev, int width)
{
	/* Set all the necessary GPH pins to special-function 3 */
	s3c_gpio_cfgrange_nopull(S3C64XX_GPH(6), width, S3C_GPIO_SFN(3));

	/* Set all the necessary GPC pins to special-function 3 */
	s3c_gpio_cfgrange_nopull(S3C64XX_GPC(4), 2, S3C_GPIO_SFN(3));
}
