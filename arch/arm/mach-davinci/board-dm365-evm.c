/*
 * TI DaVinci DM365 EVM board support
 *
 * Copyright (C) 2009 Texas Instruments Incorporated
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/dma-mapping.h>
#include <linux/i2c.h>
#include <linux/io.h>
#include <linux/clk.h>

#include <asm/setup.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>

#include <mach/hardware.h>
#include <mach/dm365.h>
#include <mach/psc.h>
#include <mach/common.h>
#include <mach/i2c.h>
#include <linux/i2c.h>
#include <mach/serial.h>
#include <mach/common.h>

static struct davinci_i2c_platform_data i2c_pdata = {
	.bus_freq	= 400	/* kHz */,
	.bus_delay	= 0	/* usec */,
};

static void __init evm_init_i2c(void)
{
	davinci_init_i2c(&i2c_pdata);
}

static struct davinci_uart_config uart_config __initdata = {
	.enabled_uarts = (1 << 0),
};

static void __init dm365_evm_map_io(void)
{
	dm365_init();
}

static __init void dm365_evm_init(void)
{
	evm_init_i2c();
	davinci_serial_init(&uart_config);
}

static __init void dm365_evm_irq_init(void)
{
	davinci_irq_init();
}

MACHINE_START(DAVINCI_DM365_EVM, "DaVinci DM365 EVM")
	.phys_io	= IO_PHYS,
	.io_pg_offst	= (__IO_ADDRESS(IO_PHYS) >> 18) & 0xfffc,
	.boot_params	= (0x80000100),
	.map_io		= dm365_evm_map_io,
	.init_irq	= dm365_evm_irq_init,
	.timer		= &davinci_timer,
	.init_machine	= dm365_evm_init,
MACHINE_END

