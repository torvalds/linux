/*
 * Copyright (C) 2010,2011 Google, Inc.
 *
 * Author:
 *	Colin Cross <ccross@android.com>
 *	Erik Gilling <ccross@android.com>
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

#ifndef __MACH_TEGRA_DEVICES_H
#define __MACH_TEGRA_DEVICES_H

#include <linux/platform_device.h>
#include <linux/platform_data/tegra_usb.h>

#include <mach/usb_phy.h>

extern struct tegra_ulpi_config tegra_ehci2_ulpi_phy_config;

extern struct tegra_ehci_platform_data tegra_ehci1_pdata;
extern struct tegra_ehci_platform_data tegra_ehci2_pdata;
extern struct tegra_ehci_platform_data tegra_ehci3_pdata;

extern struct platform_device tegra_gpio_device;
extern struct platform_device tegra_pinmux_device;
extern struct platform_device tegra_sdhci_device1;
extern struct platform_device tegra_sdhci_device2;
extern struct platform_device tegra_sdhci_device3;
extern struct platform_device tegra_sdhci_device4;
extern struct platform_device tegra_i2c_device1;
extern struct platform_device tegra_i2c_device2;
extern struct platform_device tegra_i2c_device3;
extern struct platform_device tegra_i2c_device4;
extern struct platform_device tegra_spi_device1;
extern struct platform_device tegra_spi_device2;
extern struct platform_device tegra_spi_device3;
extern struct platform_device tegra_spi_device4;
extern struct platform_device tegra_ehci1_device;
extern struct platform_device tegra_ehci2_device;
extern struct platform_device tegra_ehci3_device;
extern struct platform_device tegra_uarta_device;
extern struct platform_device tegra_uartb_device;
extern struct platform_device tegra_uartc_device;
extern struct platform_device tegra_uartd_device;
extern struct platform_device tegra_uarte_device;
extern struct platform_device tegra_pmu_device;
extern struct platform_device tegra_i2s_device1;
extern struct platform_device tegra_i2s_device2;
extern struct platform_device tegra_das_device;
extern struct platform_device tegra_pcm_device;

#endif
