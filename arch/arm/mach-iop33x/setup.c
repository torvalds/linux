/*
 * linux/arch/arm/mach-iop33x/setup.c
 *
 * Author: Dave Jiang (dave.jiang@intel.com)
 * Copyright (C) 2004 Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */
#include <linux/mm.h>
#include <linux/init.h>
#include <linux/major.h>
#include <linux/fs.h>
#include <linux/platform_device.h>
#include <linux/serial.h>
#include <linux/tty.h>
#include <linux/serial_8250.h>

#include <asm/io.h>
#include <asm/pgtable.h>
#include <asm/page.h>
#include <asm/mach/map.h>
#include <asm/setup.h>
#include <asm/system.h>
#include <asm/memory.h>
#include <asm/hardware.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/hardware/iop3xx.h>

#define IOP331_UART_XTAL 33334000

static struct resource iop33x_uart0_resources[] = {
	[0] = {
		.start = IOP331_UART0_PHYS,
		.end = IOP331_UART0_PHYS + 0x3f,
		.flags = IORESOURCE_MEM,
	},
	[1] = {
		.start = IRQ_IOP331_UART0,
		.end = IRQ_IOP331_UART0,
		.flags = IORESOURCE_IRQ
	}
};

static struct resource iop33x_uart1_resources[] = {
	[0] = {
		.start = IOP331_UART1_PHYS,
		.end = IOP331_UART1_PHYS + 0x3f,
		.flags = IORESOURCE_MEM,
	},
	[1] = {
		.start = IRQ_IOP331_UART1,
		.end = IRQ_IOP331_UART1,
		.flags = IORESOURCE_IRQ
	}
};

static struct plat_serial8250_port iop33x_uart0_data[] = {
	{
       .membase     = (char*)(IOP331_UART0_VIRT),
       .mapbase     = (IOP331_UART0_PHYS),
       .irq         = IRQ_IOP331_UART0,
       .uartclk     = IOP331_UART_XTAL,
       .regshift    = 2,
       .iotype      = UPIO_MEM,
       .flags       = UPF_SKIP_TEST,
	},
	{  },
};

static struct plat_serial8250_port iop33x_uart1_data[] = {
	{
       .membase     = (char*)(IOP331_UART1_VIRT),
       .mapbase     = (IOP331_UART1_PHYS),
       .irq         = IRQ_IOP331_UART1,
       .uartclk     = IOP331_UART_XTAL,
       .regshift    = 2,
       .iotype      = UPIO_MEM,
       .flags       = UPF_SKIP_TEST,
	},
	{  },
};

static struct platform_device iop33x_uart0 = {
       .name = "serial8250",
       .id = PLAT8250_DEV_PLATFORM,
       .dev.platform_data = iop33x_uart0_data,
       .num_resources = 2,
       .resource = iop33x_uart0_resources,
};

static struct platform_device iop33x_uart1 = {
       .name = "serial8250",
       .id = PLAT8250_DEV_PLATFORM1,
       .dev.platform_data = iop33x_uart1_data,
       .num_resources = 2,
       .resource = iop33x_uart1_resources,
};

static struct resource iop33x_i2c_0_resources[] = {
	[0] = {
		.start = 0xfffff680,
		.end = 0xfffff698,
		.flags = IORESOURCE_MEM,
	},
	[1] = {
		.start = IRQ_IOP331_I2C_0,
		.end = IRQ_IOP331_I2C_0,
		.flags = IORESOURCE_IRQ
	}
};

static struct resource iop33x_i2c_1_resources[] = {
	[0] = {
		.start = 0xfffff6a0,
		.end = 0xfffff6b8,
		.flags = IORESOURCE_MEM,
	},
	[1] = {
		.start = IRQ_IOP331_I2C_1,
		.end = IRQ_IOP331_I2C_1,
		.flags = IORESOURCE_IRQ
	}
};

static struct platform_device iop33x_i2c_0_controller = {
	.name = "IOP3xx-I2C",
	.id = 0,
	.num_resources = 2,
	.resource = iop33x_i2c_0_resources
};

static struct platform_device iop33x_i2c_1_controller = {
	.name = "IOP3xx-I2C",
	.id = 1,
	.num_resources = 2,
	.resource = iop33x_i2c_1_resources
};

static struct platform_device *iop33x_devices[] __initdata = {
	&iop33x_uart0,
	&iop33x_uart1,
	&iop33x_i2c_0_controller,
	&iop33x_i2c_1_controller
};

void __init iop33x_init(void)
{
	if(iop_is_331())
	{
		platform_add_devices(iop33x_devices,
				ARRAY_SIZE(iop33x_devices));
	}
}

#ifdef CONFIG_ARCH_IOP33X
extern void iop331_init_irq(void);
extern struct sys_timer iop331_timer;
#endif

#if defined(CONFIG_ARCH_IQ80331)
MACHINE_START(IQ80331, "Intel IQ80331")
	/* Maintainer: Intel Corp. */
	.phys_io	= 0xfefff000,
	.io_pg_offst	= ((0xfffff000) >> 18) & 0xfffc, // virtual, physical
	.map_io		= iop3xx_map_io,
	.init_irq	= iop331_init_irq,
	.timer		= &iop331_timer,
	.boot_params	= 0x0100,
	.init_machine	= iop33x_init,
MACHINE_END

#elif defined(CONFIG_MACH_IQ80332)
MACHINE_START(IQ80332, "Intel IQ80332")
	/* Maintainer: Intel Corp. */
	.phys_io	= 0xfefff000,
	.io_pg_offst	= ((0xfffff000) >> 18) & 0xfffc, // virtual, physical
	.map_io		= iop3xx_map_io,
	.init_irq	= iop331_init_irq,
	.timer		= &iop331_timer,
	.boot_params	= 0x0100,
	.init_machine	= iop33x_init,
MACHINE_END

#else
#error No machine descriptor defined for this IOP3XX implementation
#endif


