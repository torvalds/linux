/* linux/arch/arm/mach-rk2818/devices.h
 *
 * Copyright (C) 2010 ROCKCHIP, Inc.
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

#ifndef __ARCH_ARM_MACH_RK2818_DEVICES_H
#define __ARCH_ARM_MACH_RK2818_DEVICES_H

extern struct platform_device rk2818_device_uart0;
extern struct platform_device rk2818_device_uart1;
extern struct platform_device rk2818_device_uart2;
extern struct platform_device rk2818_device_uart3;
extern struct rk2818_serial_platform_data rk2818_serial0_platdata;
extern struct rk2818_serial_platform_data rk2818_serial2_platdata;
extern struct platform_device rk2818_device_spim;
extern struct rk2818_spi_platform_data rk2818_spi_platdata;
extern struct platform_device rk2818_device_i2c0;
extern struct platform_device rk2818_device_i2c1;
extern struct platform_device rk2818_device_i2c2;
extern struct platform_device rk2818_device_i2c3;
extern struct rk2818_i2c_platform_data default_i2c0_data;
extern struct rk2818_i2c_platform_data default_i2c1_data;
extern struct rk2818_i2c_spi_data default_i2c2_data;
extern struct rk2818_i2c_spi_data default_i2c3_data;
extern struct rk2818_bl_info rk2818_bl_info;
extern struct rk2818_nand_platform_data rk2818_nand_data;

extern struct soc_camera_link rk2818_iclink;                /* ddl@rock-chips.com : camera support */
extern struct rk28camera_platform_data rk28_camera_platform_data;

extern struct platform_device rk2818_device_sdmmc0;
extern struct platform_device rk2818_device_sdmmc1;
extern struct rk2818_sdmmc_platform_data default_sdmmc0_data;
extern struct rk2818_sdmmc_platform_data default_sdmmc1_data;
extern struct platform_device rk2818_jogball_device;
extern struct platform_device rk2818_device_i2s;
extern struct rk2818_i2s_platform_data rk2818_i2s_platdata;
extern struct platform_device rk2818_device_pmem;
extern struct platform_device rk2818_device_pmem_dsp;
extern struct platform_device rk2818_device_fb;
extern struct platform_device rk2818_device_adc;
extern struct platform_device rk2818_device_adckey;
extern struct rk2818_adckey_platform_data rk2818_adckey_platdata;
extern struct platform_device rk2818_device_battery;
extern struct rk2818_battery_platform_data rk2818_battery_platdata;
extern struct platform_device rk2818_device_backlight;
extern struct platform_device rk2818_device_camera;             /* ddl@rock-chips.com : camera support */
extern struct platform_device rk2818_soc_camera_pdrv;
extern struct platform_device rk2818_device_dsp;
extern struct platform_device rk2818_device_rfkill;
extern struct platform_device rk2818_nand_device;
extern struct platform_device rk2818_device_dwc_otg;
extern struct platform_device rk2818_device_host11;
extern struct platform_device android_usb_device;
extern struct platform_device usb_mass_storage_device;
extern struct platform_device rk28_device_vibrator;
extern struct timed_gpio_platform_data rk28_vibrator_info;

#endif
