/*
 * File:         arch/blackfin/mach-bf548/boards/ezkit.c
 * Based on:     arch/blackfin/mach-bf537/boards/ezkit.c
 * Author:       Aidan Williams <aidan@nicta.com.au>
 *
 * Created:
 * Description:
 *
 * Modified:
 *               Copyright 2005 National ICT Australia (NICTA)
 *               Copyright 2004-2007 Analog Devices Inc.
 *
 * Bugs:         Enter bugs at http://blackfin.uclinux.org/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see the file COPYING, or write
 * to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <linux/spi/spi.h>
#include <linux/spi/flash.h>
#include <linux/irq.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <asm/bfin5xx_spi.h>

/*
 * Name the Board for the /proc/cpuinfo
 */
char *bfin_board_name = "ADSP-BF548-EZKIT";

/*
 *  Driver needs to know address, irq and flag pin.
 */

#if defined(CONFIG_RTC_DRV_BFIN) || defined(CONFIG_RTC_DRV_BFIN_MODULE)
static struct platform_device rtc_device = {
	.name = "rtc-bfin",
	.id   = -1,
};
#endif

#if defined(CONFIG_SERIAL_BFIN) || defined(CONFIG_SERIAL_BFIN_MODULE)
static struct resource bfin_uart_resources[] = {
#ifdef CONFIG_SERIAL_BFIN_UART0
	{
		.start = 0xFFC00400,
		.end = 0xFFC004FF,
		.flags = IORESOURCE_MEM,
	},
#endif
#ifdef CONFIG_SERIAL_BFIN_UART1
	{
		.start = 0xFFC02000,
		.end = 0xFFC020FF,
		.flags = IORESOURCE_MEM,
	},
#endif
#ifdef CONFIG_SERIAL_BFIN_UART2
	{
		.start = 0xFFC02100,
		.end = 0xFFC021FF,
		.flags = IORESOURCE_MEM,
	},
#endif
#ifdef CONFIG_SERIAL_BFIN_UART3
	{
		.start = 0xFFC03100,
		.end = 0xFFC031FF,
	},
#endif
};

static struct platform_device bfin_uart_device = {
	.name = "bfin-uart",
	.id = 1,
	.num_resources = ARRAY_SIZE(bfin_uart_resources),
	.resource = bfin_uart_resources,
};
#endif

static struct platform_device *ezkit_devices[] __initdata = {
#if defined(CONFIG_RTC_DRV_BFIN) || defined(CONFIG_RTC_DRV_BFIN_MODULE)
	&rtc_device,
#endif

#if defined(CONFIG_SERIAL_BFIN) || defined(CONFIG_SERIAL_BFIN_MODULE)
	&bfin_uart_device,
#endif
};

static int __init stamp_init(void)
{
	printk(KERN_INFO "%s(): registering device resources\n", __FUNCTION__);
	platform_add_devices(ezkit_devices, ARRAY_SIZE(ezkit_devices));
	return 0;
}

arch_initcall(stamp_init);
