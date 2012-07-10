/* arch/arm/mach-rk30/devices.c
 *
 * Copyright (C) 2012 ROCKCHIP, Inc.
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

#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#ifdef CONFIG_USB_ANDROID
#include <linux/usb/android_composite.h>
#endif
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <asm/pmu.h>
#include <mach/irqs.h>
#include <mach/board.h>
#include <plat/dma-pl330.h>
#include <mach/gpio.h>
//#include <mach/iomux.h>
#include <plat/rk_fiq_debugger.h>

static int __init rk2928_init_devices(void)
{
#if defined(CONFIG_FIQ_DEBUGGER) && defined(DEBUG_UART_PHYS)
	rk_serial_debug_init(DEBUG_UART_BASE, IRQ_DEBUG_UART, IRQ_UART_SIGNAL, -1);
#endif
	return 0;
}
arch_initcall(rk2928_init_devices);
