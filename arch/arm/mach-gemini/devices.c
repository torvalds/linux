/*
 * Common devices definition for Gemini
 *
 * Copyright (C) 2008-2009 Paulius Zaleckas <paulius.zaleckas@teltonika.lt>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/serial_8250.h>
#include <linux/mtd/physmap.h>

#include <mach/irqs.h>
#include <mach/hardware.h>
#include <mach/global_reg.h>

static struct plat_serial8250_port serial_platform_data[] = {
	{
		.membase	= (void *)IO_ADDRESS(GEMINI_UART_BASE),
		.mapbase	= GEMINI_UART_BASE,
		.irq		= IRQ_UART,
		.uartclk	= UART_CLK,
		.regshift	= 2,
		.iotype		= UPIO_MEM,
		.type		= PORT_16550A,
		.flags		= UPF_BOOT_AUTOCONF | UPF_SKIP_TEST | UPF_FIXED_TYPE,
	},
	{},
};

static struct platform_device serial_device = {
	.name	= "serial8250",
	.id	= PLAT8250_DEV_PLATFORM,
	.dev	= {
		.platform_data = serial_platform_data,
	},
};

int platform_register_uart(void)
{
	return platform_device_register(&serial_device);
}

static struct resource flash_resource = {
	.start	= GEMINI_FLASH_BASE,
	.flags	= IORESOURCE_MEM,
};

static struct physmap_flash_data pflash_platform_data = {};

static struct platform_device pflash_device = {
	.name	= "physmap-flash",
	.id	= 0,
	.dev 	= {
		.platform_data = &pflash_platform_data,
	},
	.resource = &flash_resource,
	.num_resources = 1,
};

int platform_register_pflash(unsigned int size, struct mtd_partition *parts,
			     unsigned int nr_parts)
{
	unsigned int reg;

	reg = __raw_readl(IO_ADDRESS(GEMINI_GLOBAL_BASE) + GLOBAL_STATUS);

	if ((reg & FLASH_TYPE_MASK) != FLASH_TYPE_PARALLEL)
		return -ENXIO;

	if (reg & FLASH_WIDTH_16BIT)
		pflash_platform_data.width = 2;
	else
		pflash_platform_data.width = 1;

	/* enable parallel flash pins and disable others */
	reg = __raw_readl(IO_ADDRESS(GEMINI_GLOBAL_BASE) + GLOBAL_MISC_CTRL);
	reg &= ~PFLASH_PADS_DISABLE;
	reg |= SFLASH_PADS_DISABLE | NAND_PADS_DISABLE;
	__raw_writel(reg, IO_ADDRESS(GEMINI_GLOBAL_BASE) + GLOBAL_MISC_CTRL);

	flash_resource.end = flash_resource.start + size - 1;

	pflash_platform_data.parts = parts;
	pflash_platform_data.nr_parts = nr_parts;

	return platform_device_register(&pflash_device);
}
