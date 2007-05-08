/*
 * File:         arch/blackfin/mach-bf533/generic_board.c
 * Based on:     arch/blackfin/mach-bf533/ezkit.c
 * Author:       Aidan Williams <aidan@nicta.com.au>
 *
 * Created:      2005
 * Description:
 *
 * Modified:
 *               Copyright 2005 National ICT Australia (NICTA)
 *               Copyright 2004-2006 Analog Devices Inc.
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
#include <asm/irq.h>

/*
 * Name the Board for the /proc/cpuinfo
 */
char *bfin_board_name = "UNKNOWN BOARD";

#if defined(CONFIG_RTC_DRV_BFIN) || defined(CONFIG_RTC_DRV_BFIN_MODULE)
static struct platform_device rtc_device = {
	.name = "rtc-bfin",
	.id   = -1,
};
#endif

/*
 *  Driver needs to know address, irq and flag pin.
 */
#if defined(CONFIG_SMC91X) || defined(CONFIG_SMC91X_MODULE)
static struct resource smc91x_resources[] = {
	{
		.start = 0x20300300,
		.end = 0x20300300 + 16,
		.flags = IORESOURCE_MEM,
	},{
		.start = IRQ_PROG_INTB,
		.end = IRQ_PROG_INTB,
		.flags = IORESOURCE_IRQ | IORESOURCE_IRQ_HIGHLEVEL,
	},{
		/*
		 *  denotes the flag pin and is used directly if
		 *  CONFIG_IRQCHIP_DEMUX_GPIO is defined.
		 */
		.start = IRQ_PF7,
		.end = IRQ_PF7,
		.flags = IORESOURCE_IRQ | IORESOURCE_IRQ_HIGHLEVEL,
	},
};

static struct platform_device smc91x_device = {
	.name = "smc91x",
	.id = 0,
	.num_resources = ARRAY_SIZE(smc91x_resources),
	.resource = smc91x_resources,
};
#endif

static struct platform_device *generic_board_devices[] __initdata = {
#if defined(CONFIG_RTC_DRV_BFIN) || defined(CONFIG_RTC_DRV_BFIN_MODULE)
	&rtc_device,
#endif

#if defined(CONFIG_SMC91X) || defined(CONFIG_SMC91X_MODULE)
	&smc91x_device,
#endif
};

static int __init generic_board_init(void)
{
	printk(KERN_INFO "%s(): registering device resources\n", __FUNCTION__);
	return platform_add_devices(generic_board_devices, ARRAY_SIZE(generic_board_devices));
}

arch_initcall(generic_board_init);
