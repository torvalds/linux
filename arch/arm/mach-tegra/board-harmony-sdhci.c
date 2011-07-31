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
#include <mach/pinmux.h>

#include "gpio-names.h"

static struct resource sdhci_resource1[] = {
	[0] = {
		.start  = INT_SDMMC1,
		.end    = INT_SDMMC1,
		.flags  = IORESOURCE_IRQ,
	},
	[1] = {
		.start  = TEGRA_SDMMC1_BASE,
		.end    = TEGRA_SDMMC1_BASE + TEGRA_SDMMC1_SIZE-1,
		.flags  = IORESOURCE_MEM,
	},
};

static struct resource sdhci_resource2[] = {
	[0] = {
		.start  = INT_SDMMC2,
		.end    = INT_SDMMC2,
		.flags  = IORESOURCE_IRQ,
	},
	[1] = {
		.start	= TEGRA_SDMMC2_BASE,
		.end	= TEGRA_SDMMC2_BASE + TEGRA_SDMMC2_SIZE-1,
		.flags	= IORESOURCE_MEM,
	},
};

static struct resource sdhci_resource4[] = {
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

static struct tegra_sdhci_platform_data tegra_sdhci_platform_data1 = {
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

static struct tegra_sdhci_platform_data tegra_sdhci_platform_data4 = {
	.clk_id = NULL,
	.force_hs = 0,
	.cd_gpio = TEGRA_GPIO_PH2,
	.wp_gpio = TEGRA_GPIO_PH3,
	.power_gpio = TEGRA_GPIO_PI6,
};

static struct platform_device tegra_sdhci_device1 = {
	.name		= "sdhci-tegra",
	.id		= 0,
	.resource	= sdhci_resource1,
	.num_resources	= ARRAY_SIZE(sdhci_resource1),
	.dev = {
		.platform_data = &tegra_sdhci_platform_data1,
	},
};

static struct platform_device tegra_sdhci_device2 = {
	.name		= "sdhci-tegra",
	.id		= 1,
	.resource	= sdhci_resource2,
	.num_resources	= ARRAY_SIZE(sdhci_resource2),
	.dev = {
		.platform_data = &tegra_sdhci_platform_data2,
	},
};

static struct platform_device tegra_sdhci_device4 = {
	.name		= "sdhci-tegra",
	.id		= 3,
	.resource	= sdhci_resource4,
	.num_resources	= ARRAY_SIZE(sdhci_resource4),
	.dev = {
		.platform_data = &tegra_sdhci_platform_data4,
	},
};

int __init harmony_sdhci_init(void)
{
	gpio_request(tegra_sdhci_platform_data2.power_gpio, "sdhci2_power");
	gpio_request(tegra_sdhci_platform_data2.cd_gpio, "sdhci2_cd");
	gpio_request(tegra_sdhci_platform_data2.wp_gpio, "sdhci2_wp");

	tegra_gpio_enable(tegra_sdhci_platform_data2.power_gpio);
	tegra_gpio_enable(tegra_sdhci_platform_data2.cd_gpio);
	tegra_gpio_enable(tegra_sdhci_platform_data2.wp_gpio);

	gpio_request(tegra_sdhci_platform_data4.power_gpio, "sdhci4_power");
	gpio_request(tegra_sdhci_platform_data4.cd_gpio, "sdhci4_cd");
	gpio_request(tegra_sdhci_platform_data4.wp_gpio, "sdhci4_wp");

	tegra_gpio_enable(tegra_sdhci_platform_data4.power_gpio);
	tegra_gpio_enable(tegra_sdhci_platform_data4.cd_gpio);
	tegra_gpio_enable(tegra_sdhci_platform_data4.wp_gpio);

	gpio_direction_output(tegra_sdhci_platform_data2.power_gpio, 1);
	gpio_direction_output(tegra_sdhci_platform_data4.power_gpio, 1);

	platform_device_register(&tegra_sdhci_device1);
	platform_device_register(&tegra_sdhci_device2);
	platform_device_register(&tegra_sdhci_device4);

	return 0;
}
