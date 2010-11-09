/* /arch/arm/mach-rk29/devices.h
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

#ifndef __ARCH_ARM_MACH_RK29_DEVICES_H
#define __ARCH_ARM_MACH_RK29_DEVICES_H

extern struct rk29_nand_platform_data rk29_nand_data;

extern struct platform_device rk29_device_uart0;
extern struct platform_device rk29_device_uart1;
extern struct platform_device rk29_device_uart2;
extern struct platform_device rk29_device_uart3;
extern struct platform_device rk29_device_fb;
extern struct platform_device rk29_device_nand;
extern struct rk29_sdmmc_platform_data default_sdmmc0_data;
extern struct rk29_sdmmc_platform_data default_sdmmc1_data;
extern struct platform_device rk29_device_sdmmc0;
extern struct platform_device rk29_device_sdmmc1;

#endif