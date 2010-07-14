/*
 * arch/arm/mach-tegra/board-harmony-sdhci.c
 *
 * Copyright (C) 2010 Google, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/resource.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/gpio.h>

#include <asm/mach-types.h>
#include <mach/irqs.h>
#include <mach/iomap.h>
#include <mach/sdhci.h>

#include "gpio-names.h"
#include "board.h"

static struct resource sdhci_resource0[] = {
	[0] = {
		.start  = INT_SDMMC1,
		.end    = INT_SDMMC1,
		.flags  = IORESOURCE_IRQ,
	},
	[1] = {
		.start	= TEGRA_SDMMC1_BASE,
		.end	= TEGRA_SDMMC1_BASE + TEGRA_SDMMC1_SIZE-1,
		.flags	= IORESOURCE_MEM,
	},
};

static struct resource sdhci_resource2[] = {
	[0] = {
		.start  = INT_SDMMC3,
		.end    = INT_SDMMC3,
		.flags  = IORESOURCE_IRQ,
	},
	[1] = {
		.start	= TEGRA_SDMMC3_BASE,
		.end	= TEGRA_SDMMC3_BASE + TEGRA_SDMMC3_SIZE-1,
		.flags	= IORESOURCE_MEM,
	},
};

static struct resource sdhci_resource3[] = {
	[0] = {
		.start  = INT_SDMMC4,
		.end    = INT_SDMMC4,
		.flags  = IORESOURCE_IRQ,
	},
	[1] = {
		.start	= TEGRA_SDMMC4_BASE,
		.end	= TEGRA_SDMMC4_BASE + TEGRA_SDMMC4_SIZE-1,
		.flags	= IORESOURCE_MEM,
	},
};

static struct tegra_sdhci_platform_data tegra_sdhci_platform_data0 = {
	.clk_id = NULL,
	.force_hs = 1,
	.cd_gpio = -1,
	.wp_gpio = -1,
	.power_gpio = -1,
};

static struct tegra_sdhci_platform_data tegra_sdhci_platform_data2 = {
	.clk_id = NULL,
	.force_hs = 1,
	.cd_gpio = TEGRA_GPIO_PI5,
	.wp_gpio = TEGRA_GPIO_PH1,
	.power_gpio = TEGRA_GPIO_PT3,
};

static struct tegra_sdhci_platform_data tegra_sdhci_platform_data3 = {
	.clk_id = NULL,
	.force_hs = 0,
	.cd_gpio = -1,
	.wp_gpio = -1,
	.power_gpio = TEGRA_GPIO_PI6,
};

static struct platform_device tegra_sdhci_device0 = {
	.name		= "sdhci-tegra",
	.id		= 0,
	.resource	= sdhci_resource0,
	.num_resources	= ARRAY_SIZE(sdhci_resource0),
	.dev = {
		.platform_data = &tegra_sdhci_platform_data0,
	},
};

static struct platform_device tegra_sdhci_device2 = {
	.name		= "sdhci-tegra",
	.id		= 2,
	.resource	= sdhci_resource2,
	.num_resources	= ARRAY_SIZE(sdhci_resource2),
	.dev = {
		.platform_data = &tegra_sdhci_platform_data2,
	},
};

static struct platform_device tegra_sdhci_device3 = {
	.name		= "sdhci-tegra",
	.id		= 3,
	.resource	= sdhci_resource3,
	.num_resources	= ARRAY_SIZE(sdhci_resource3),
	.dev = {
		.platform_data = &tegra_sdhci_platform_data3,
	},
};

int __init ventana_sdhci_init(void)
{
	gpio_request(tegra_sdhci_platform_data2.power_gpio, "sdhci2_power");
	gpio_request(tegra_sdhci_platform_data2.cd_gpio, "sdhci2_cd");
	gpio_request(tegra_sdhci_platform_data2.wp_gpio, "sdhci2_wp");
	gpio_request(tegra_sdhci_platform_data3.power_gpio, "sdhci3_power");

	tegra_gpio_enable(tegra_sdhci_platform_data2.power_gpio);
	tegra_gpio_enable(tegra_sdhci_platform_data2.cd_gpio);
	tegra_gpio_enable(tegra_sdhci_platform_data2.wp_gpio);
	tegra_gpio_enable(tegra_sdhci_platform_data3.power_gpio);

	gpio_direction_output(tegra_sdhci_platform_data2.power_gpio, 1);
	gpio_direction_output(tegra_sdhci_platform_data3.power_gpio, 1);
	gpio_set_value(tegra_sdhci_platform_data3.power_gpio, 1);

	platform_device_register(&tegra_sdhci_device3);
	platform_device_register(&tegra_sdhci_device2);
	platform_device_register(&tegra_sdhci_device0);

	return 0;
}
