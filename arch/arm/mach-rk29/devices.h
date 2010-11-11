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


extern struct platform_device rk29_device_uart0;
extern struct platform_device rk29_device_uart1;
extern struct platform_device rk29_device_uart2;
extern struct platform_device rk29_device_uart3;
extern struct platform_device rk29xx_device_spi0m;
extern struct platform_device rk29xx_device_spi1m;
extern struct rk29xx_spi_platform_data rk29xx_spi0_platdata;
extern struct rk29xx_spi_platform_data rk29xx_spi1_platdata;

#endif