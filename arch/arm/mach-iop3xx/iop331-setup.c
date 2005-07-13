/*
 * linux/arch/arm/mach-iop3xx/iop331-setup.c
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
#include <linux/config.h>
#include <linux/init.h>
#include <linux/major.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/serial.h>
#include <linux/tty.h>
#include <linux/serial_core.h>

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

#define IOP331_UART_XTAL 33334000

/*
 * Standard IO mapping for all IOP331 based systems
 */
static struct map_desc iop331_std_desc[] __initdata = {
 /* virtual     physical      length      type */

 /* mem mapped registers */
 { IOP331_VIRT_MEM_BASE,  IOP331_PHYS_MEM_BASE,   0x00002000,  MT_DEVICE },

 /* PCI IO space */
 { IOP331_PCI_LOWER_IO_VA,  IOP331_PCI_LOWER_IO_PA,   IOP331_PCI_IO_WINDOW_SIZE,  MT_DEVICE }
};

static struct uart_port iop331_serial_ports[] = {
	{
		.membase	= (char*)(IOP331_UART0_VIRT),
		.mapbase	= (IOP331_UART0_PHYS),
		.irq		= IRQ_IOP331_UART0,
		.flags		= UPF_SKIP_TEST,
		.iotype		= UPIO_MEM,
		.regshift	= 2,
		.uartclk	= IOP331_UART_XTAL,
		.line		= 0,
		.type		= PORT_XSCALE,
		.fifosize	= 32
	} , {
		.membase	= (char*)(IOP331_UART1_VIRT),
		.mapbase	= (IOP331_UART1_PHYS),
		.irq		= IRQ_IOP331_UART1,
		.flags		= UPF_SKIP_TEST,
		.iotype		= UPIO_MEM,
		.regshift	= 2,
		.uartclk	= IOP331_UART_XTAL,
		.line		= 1,
		.type		= PORT_XSCALE,
		.fifosize	= 32
	}
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

void __init iop331_map_io(void)
{
	iotable_init(iop331_std_desc, ARRAY_SIZE(iop331_std_desc));
	early_serial_setup(&iop331_serial_ports[0]);
	early_serial_setup(&iop331_serial_ports[1]);
}

#ifdef CONFIG_ARCH_IOP331
extern void iop331_init_irq(void);
extern struct sys_timer iop331_timer;
#endif

#ifdef CONFIG_ARCH_IQ80331
extern void iq80331_map_io(void);
#endif

#ifdef CONFIG_MACH_IQ80332
extern void iq80332_map_io(void);
#endif

#if defined(CONFIG_ARCH_IQ80331)
MACHINE_START(IQ80331, "Intel IQ80331")
	/* Maintainer: Intel Corp. */
	.phys_ram	= PHYS_OFFSET,
	.phys_io	= 0xfefff000,
	.io_pg_offst	= ((0xfffff000) >> 18) & 0xfffc, // virtual, physical
	.map_io		= iq80331_map_io,
	.init_irq	= iop331_init_irq,
	.timer		= &iop331_timer,
	.boot_params	= 0x0100,
	.init_machine	= iop33x_init,
MACHINE_END

#elif defined(CONFIG_MACH_IQ80332)
MACHINE_START(IQ80332, "Intel IQ80332")
	/* Maintainer: Intel Corp. */
	.phys_ram	= PHYS_OFFSET,
	.phys_io	= 0xfefff000,
	.io_pg_offst	= ((0xfffff000) >> 18) & 0xfffc, // virtual, physical
	.map_io		= iq80332_map_io,
	.init_irq	= iop331_init_irq,
	.timer		= &iop331_timer,
	.boot_params	= 0x0100,
	.init_machine	= iop33x_init,
MACHINE_END

#else
#error No machine descriptor defined for this IOP3XX implementation
#endif


