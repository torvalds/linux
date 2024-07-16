// SPDX-License-Identifier: GPL-2.0
//
// Copyright (c) 2010 Samsung Electronics Co., Ltd.
//		http://www.samsung.com/
//
// S3C64XX setup information for IDE

#include <linux/kernel.h>
#include <linux/gpio.h>
#include <linux/io.h>

#include <linux/platform_data/ata-samsung_cf.h>

#include "map.h"
#include "regs-clock.h"
#include "gpio-cfg.h"
#include "gpio-samsung.h"

void s3c64xx_ide_setup_gpio(void)
{
	u32 reg;

	reg = readl(S3C_MEM_SYS_CFG) & (~0x3f);

	/* Independent CF interface, CF chip select configuration */
	writel(reg | MEM_SYS_CFG_INDEP_CF |
		MEM_SYS_CFG_EBI_FIX_PRI_CFCON, S3C_MEM_SYS_CFG);

	s3c_gpio_cfgpin(S3C64XX_GPB(4), S3C_GPIO_SFN(4));

	/* Set XhiDATA[15:0] pins as CF Data[15:0] */
	s3c_gpio_cfgpin_range(S3C64XX_GPK(0), 16, S3C_GPIO_SFN(5));

	/* Set XhiADDR[2:0] pins as CF ADDR[2:0] */
	s3c_gpio_cfgpin_range(S3C64XX_GPL(0), 3, S3C_GPIO_SFN(6));

	/* Set Xhi ctrl pins as CF ctrl pins(IORDY, IOWR, IORD, CE[0:1]) */
	s3c_gpio_cfgpin(S3C64XX_GPM(5), S3C_GPIO_SFN(1));
	s3c_gpio_cfgpin_range(S3C64XX_GPM(0), 5, S3C_GPIO_SFN(6));
}
