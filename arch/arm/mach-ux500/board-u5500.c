/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * Author: Rabin Vincent <rabin.vincent@stericsson.com> for ST-Ericsson
 * License terms: GNU General Public License (GPL) version 2
 */

#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/amba/bus.h>
#include <linux/gpio.h>
#include <linux/irq.h>

#include <asm/mach/arch.h>
#include <asm/mach-types.h>

#include <mach/hardware.h>
#include <mach/devices.h>
#include <mach/setup.h>

#include "devices-db5500.h"

static void __init u5500_uart_init(void)
{
	db5500_add_uart0();
	db5500_add_uart1();
	db5500_add_uart2();
}

static void __init u5500_init_machine(void)
{
	u5500_init_devices();

	u5500_uart_init();
}

MACHINE_START(U8500, "ST-Ericsson U5500 Platform")
	.boot_params	= 0x00000100,
	.map_io		= u5500_map_io,
	.init_irq	= ux500_init_irq,
	.timer		= &ux500_timer,
	.init_machine	= u5500_init_machine,
MACHINE_END
